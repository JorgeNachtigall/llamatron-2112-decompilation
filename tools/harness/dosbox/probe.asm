; PROBE.COM - DOS-side helper for DOSBox-X reference runs (docs/PLAN.md §3.3).
; Test-oracle code only, never part of src/. Our own code; it contains nothing from
; the original. Assemble: nasm -f bin -o PROBE.COM probe.asm
;
;   PROBE tttttttt [v [f [iiiiiiii [mmmm]]]]
;     tttttttt  BIOS tick count to pin (hex) = the game's RNG seed source
;     v         VGA flag for PSP:86h (0/1, default 1); VRAM snapshots need v=1
;     f         flash option for PSP:85h (0 none, 1 'P', 2 'Y'; default 0)
;     iiiiiiii  snapshot interval in timer IRQs (hex, 0 = no periodic snapshots)
;     mmmm      maximum number of snapshots (hex, at most MAX_SNAPS)
;   optional PROBE.KEY in the current directory: scripted keyboard input, records of
;   6 bytes {u32 irq number, u8 scancode, u8 0}, sorted by irq number.
;   optional PROBE.POK: scripted state changes (scenarios that keys cannot reach, e.g. a
;   warp), records of 8 bytes {u32 irq number, u16 DGROUP offset, u16 value}, sorted by
;   irq number; each stores the word at DS:offset right before that IRQ's keys.
;
; What it does:
;  1. Masks IRQ0 and writes tttttttt to 0040:006C, so the BIOS tick word stays
;     constant while game86 seeds (1000:5f6b) and shuffles its random table
;     (1000:007b..00ab, which re-reads the tick on every mixer call).
;  2. INT 10h hook: game86's first INT 10h AX=000Dh (video_set_mode from 1000:5960)
;     comes right after the shuffle. The hook copies the caller's DS (DGROUP, 64 KB)
;     to a buffer (PROBE.DG) and unmasks IRQ0.
;  3. INT 21h hook while the game runs: when game86 installs its timer handler
;     (INT 21h AX=2508h from 1000:e55b) the probe installs irq08_wrapper instead and
;     calls the game's handler from it. For every timer IRQ (numbered from 1) the
;     wrapper
;       - applies the scripted pokes, then the scripted keys due at this IRQ, with the same effect as the
;         game's keyboard handler (1000:e567..e5e9), BEFORE the game's tick runs;
;       - after the tick, every iiiiiiii IRQs copies DGROUP into XMS;
;       - after such a snapshot, copies VRAM (planes 0..3, offsets 0..5F3Fh = the
;         three pages) at the next IRQ that interrupted the game inside its vsync
;         wait (1000:e5f3..e604), so no drawing sequence is cut in half. VGA only:
;         the GC registers it touches are read back and restored.
;  4. EXECs game86.exe with the launcher's command-tail contract (llama.exe
;     1000:0103; re-notes §1): PSP:80h=0, 81h='l', 82h='m', 83h=CR, 84h=0 (8086 build),
;     85h=flash, 86h=VGA, 88h=0 (keyboard), 89h=FFh (no sound driver).
;  5. After the game exits: PROBE.HDR, PROBE.DG and (if any snapshots) PROBE.SNP.
;
; PROBE.HDR (little endian, version 3):
;   +0 'LTPB'  +4 u16 version  +6 u16 DGROUP segment  +8 u32 tick pinned
;   +12 u32 tick word at the INT 10h snapshot  +16 u8 INT 10h snapshot taken
;   +17 u8 game exit code  +18 u8 exit type (INT 21h/4Dh AH; FFh = EXEC failed)
;   +19 u8 EXEC error  +20 u16 periodic snapshots taken  +22 u8 XMS available
;   +23 u8 timer wrap state (0 never, 1 wrapped, 2 game restored its vector)
;   +24 u32 timer IRQs seen  +28 u16 scripted keys applied  +30 u16 keys loaded
;   +32 u16 scripted pokes applied  +34 u16 pokes loaded
; PROBE.SNP: header {'LTSN', u16 version 1, u16 count, u32 interval, u16 flags
;   (bit0 VRAM present), u32 DGROUP bytes, u32 VRAM bytes}, then count x {u32 IRQ of
;   the DGROUP copy, u32 IRQ of the VRAM copy (0 = none)}, then per snapshot the
;   DGROUP bytes followed by the VRAM bytes (plane 0..3, VRAM_PLANE bytes each).

        cpu     8086
        org     100h

DG_SIZE         equ     10000h
VRAM_PLANE      equ     5F40h           ; pages 0000h, 2000h, 4000h (8000 bytes each)
VRAM_SIZE       equ     4 * VRAM_PLANE
SLOT_KB         equ     160             ; (DG_SIZE + VRAM_SIZE) rounded up to KB
SLOT_LOW        equ     7D00h           ; slot size = 2 * 10000h + SLOT_LOW
MAX_SNAPS       equ     200
MAX_KEYS        equ     4000
KEY_REC         equ     6
MAX_POKES       equ     256
POKE_REC        equ     8
VSYNC_FIRST     equ     0E5F3h          ; game86 1000:e5f3 vsync wait, RETF at e604
VSYNC_LAST      equ     0E604h
KBD_LAST_RAW    equ     0587h           ; game86 DGROUP variables written by 1000:e567
KBD_LAST_MAKE   equ     0586h
KBD_LAST_MAKE2  equ     0B72h
KBD_BITS        equ     0588h

start:
        mov     sp, stack_top
        cld
        xor     ax, ax                  ; .bss is not cleared by DOS
        mov     [key_pos], ax
        mov     [poke_pos], ax
        mov     si, 81h                 ; our command tail
        call    skip_spaces
        call    parse_hex32
        mov     [ticks], ax
        mov     [ticks+2], dx
        call    skip_spaces
        call    parse_digit
        jc      .no_vga
        mov     [tail+6], al
.no_vga:
        call    skip_spaces
        call    parse_digit
        jc      .no_flash
        mov     [tail+5], al
.no_flash:
        call    skip_spaces
        call    parse_hex32
        mov     [interval], ax
        mov     [interval+2], dx
        mov     [next_snap], ax
        mov     [next_snap+2], dx
        call    skip_spaces
        call    parse_hex32
        cmp     ax, MAX_SNAPS
        jbe     .max_ok
        mov     ax, MAX_SNAPS
.max_ok:
        mov     [max_snaps], ax
        mov     al, [tail+6]
        mov     [vga], al

        ; give memory back to DOS: keep the program, its .bss and the stack
        mov     bx, prog_end + 15
        mov     cl, 4
        shr     bx, cl
        mov     ah, 4Ah                 ; ES = our PSP
        int     21h
        jc      fatal
        mov     ah, 48h                 ; 64 KB snapshot / transfer buffer
        mov     bx, 1000h
        int     21h
        jc      fatal
        mov     [bufseg], ax
        mov     ah, 48h                 ; key script buffer
        mov     bx, (MAX_KEYS * KEY_REC + 15) / 16
        int     21h
        jc      fatal
        mov     [keyseg], ax

        call    load_keys
        call    load_pokes
        call    setup_xms

        mov     ax, 3510h               ; hook INT 10h
        int     21h
        mov     [old10], bx
        mov     [old10+2], es
        mov     dx, int10_hook
        mov     ax, 2510h
        int     21h
        mov     ax, 3521h               ; hook INT 21h
        int     21h
        mov     [old21], bx
        mov     [old21+2], es
        mov     dx, int21_hook
        mov     ax, 2521h
        int     21h

        cli                             ; pin the BIOS tick
        in      al, 21h
        mov     [pic_mask], al
        or      al, 1
        out     21h, al
        xor     ax, ax
        mov     es, ax
        mov     ax, [ticks]
        mov     [es:46Ch], ax
        mov     ax, [ticks+2]
        mov     [es:46Eh], ax
        sti

        mov     [epb_tail+2], cs
        mov     [epb_fcb1+2], cs
        mov     [epb_fcb2+2], cs
        push    cs
        pop     es
        mov     bx, epb
        mov     dx, game_name
        mov     byte [in_game], 1
        mov     ax, 4B00h
        int     21h
        cli                             ; EXEC may clobber everything but CS:IP
        mov     bx, cs
        mov     ss, bx
        mov     sp, stack_top
        sti
        mov     ds, bx
        mov     es, bx
        mov     byte [in_game], 0
        jnc     .ran
        mov     [exec_error], al
        mov     byte [exit_type], 0FFh
        jmp     .restore
.ran:
        mov     ah, 4Dh
        int     21h
        mov     [exit_code], al
        mov     [exit_type], ah

.restore:
        in      al, 21h                 ; IRQ0 back to how we found it
        and     al, 0FEh
        mov     ah, [pic_mask]
        and     ah, 1
        or      al, ah
        out     21h, al
        push    ds
        lds     dx, [old21]
        mov     ax, 2521h
        int     21h
        pop     ds
        push    ds
        lds     dx, [old10]
        mov     ax, 2510h
        int     21h
        pop     ds

        mov     ax, [nsnaps]
        mov     [hdr_nsnaps], ax
        mov     dx, hdr_name            ; PROBE.HDR
        call    create_file
        jc      fatal
        mov     dx, header
        mov     cx, header_end - header
        call    write_chunk
        call    close_file

        cmp     byte [snap_taken], 0
        je      .no_dg
        mov     dx, dg_name             ; PROBE.DG
        call    create_file
        jc      fatal
        push    ds
        mov     ds, [cs:bufseg]
        xor     dx, dx
        mov     cx, 8000h
        call    write_chunk
        mov     dx, 8000h
        mov     cx, 8000h
        call    write_chunk
        pop     ds
        call    close_file
.no_dg:
        call    write_snapshots
        cmp     byte [xms_ok], 0
        je      .done
        mov     dx, [xms_handle]        ; free the XMS block
        mov     ah, 0Ah
        call    far [xms_entry]
.done:
        mov     ax, 4C00h
        int     21h

fatal:
        mov     ax, 4C01h
        int     21h

; ---- set-up ----------------------------------------------------------------------

load_keys:                              ; PROBE.KEY -> keyseg, nkeys
        mov     dx, key_name
        mov     ax, 3D00h
        int     21h
        jc      .none
        mov     [handle], ax
        push    ds
        mov     bx, ax
        mov     ds, [cs:keyseg]
        xor     dx, dx
        mov     cx, MAX_KEYS * KEY_REC
        mov     ah, 3Fh
        int     21h
        pop     ds
        jc      .close
        xor     dx, dx
        mov     cx, KEY_REC
        div     cx
        mov     [nkeys], ax
.close:
        call    close_file
.none:
        ret

load_pokes:                             ; PROBE.POK -> pokes, npokes
        mov     dx, poke_name
        mov     ax, 3D00h
        int     21h
        jc      .none
        mov     [handle], ax
        mov     bx, ax
        mov     dx, pokes
        mov     cx, MAX_POKES * POKE_REC
        mov     ah, 3Fh
        int     21h
        jc      .close
        mov     cl, 3
        shr     ax, cl                  ; / POKE_REC
        mov     [npokes], ax
.close:
        call    close_file
.none:
        ret

setup_xms:                              ; XMS block for max_snaps slots
        cmp     word [max_snaps], 0
        je      .off
        mov     ax, [interval]
        or      ax, [interval+2]
        jz      .off
        mov     ax, 4300h
        int     2Fh
        cmp     al, 80h
        jne     .off
        push    es
        mov     ax, 4310h
        int     2Fh
        mov     [xms_entry], bx
        mov     [xms_entry+2], es
        pop     es
        mov     ax, [max_snaps]
        mov     dx, SLOT_KB
        mul     dx
        mov     dx, ax
        mov     ah, 09h
        call    far [xms_entry]
        cmp     ax, 1
        jne     .off
        mov     [xms_handle], dx
        mov     byte [xms_ok], 1
        ret
.off:
        mov     word [max_snaps], 0
        ret

; ---- INT 10h hook ------------------------------------------------------------------

int10_hook:
        cmp     ax, 000Dh
        jne     .chain
        cmp     byte [cs:snap_taken], 0
        jne     .chain
        push    ax
        push    cx
        push    si
        push    di
        push    ds
        push    es
        mov     [cs:dgroup_seg], ds
        mov     es, [cs:bufseg]
        xor     si, si
        xor     di, di
        mov     cx, 8000h
        cld
        rep     movsw
        xor     ax, ax
        mov     ds, ax
        mov     ax, [46Ch]
        mov     [cs:ticks_seen], ax
        mov     ax, [46Eh]
        mov     [cs:ticks_seen+2], ax
        mov     byte [cs:snap_taken], 1
        in      al, 21h                 ; release the timer
        and     al, 0FEh
        out     21h, al
        pop     es
        pop     ds
        pop     di
        pop     si
        pop     cx
        pop     ax
.chain:
        jmp     far [cs:old10]

; ---- INT 21h hook: wrap the game's timer handler ------------------------------------

int21_hook:
        cmp     byte [cs:in_game], 0
        je      .chain
        cmp     ax, 2508h
        jne     .chain
        cmp     byte [cs:wrapped], 0
        jne     .unwrap
        mov     [cs:game08], dx         ; game86 1000:e55b: DS:DX = CS:3de4
        mov     [cs:game08+2], ds
        mov     [cs:game_cs], ds
        mov     byte [cs:wrapped], 1
        push    ds
        push    dx
        push    cs
        pop     ds
        mov     dx, irq08_wrapper
        pushf
        call    far [cs:old21]
        pop     dx
        pop     ds
        iret
.unwrap:                                ; the game restores the BIOS vector at exit
        mov     byte [cs:wrapped], 2
.chain:
        jmp     far [cs:old21]

; ---- timer wrapper -------------------------------------------------------------------

irq08_wrapper:
        push    ax
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        push    bp
        push    ds
        push    es
        mov     bp, sp                  ; [bp+18] IP, [bp+20] CS of the interrupted code
        push    cs
        pop     ds
        inc     byte [depth]
        add     word [irq_count], 1
        adc     word [irq_count+2], 0
        call    apply_pokes
        call    apply_keys
        pushf
        call    far [game08]
        push    cs
        pop     ds
        cmp     byte [depth], 1         ; a nested IRQ never snapshots mid-tick
        jne     .out
        call    maybe_snapshot
.out:
        dec     byte [depth]
        pop     es
        pop     ds
        pop     bp
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        iret

apply_keys:                             ; scripted keys with irq <= irq_count
        mov     ax, [dgroup_seg]
        or      ax, ax
        jz      .ret
        mov     es, ax
        mov     si, [key_pos]
.next:
        cmp     si, [cs:nkeys]
        jae     .done
        mov     bx, si
        shl     bx, 1
        add     bx, si
        shl     bx, 1                   ; bx = si * KEY_REC
        push    ds
        mov     ds, [cs:keyseg]
        mov     ax, [bx]
        mov     dx, [bx+2]
        mov     cl, [bx+4]
        pop     ds
        cmp     dx, [irq_count+2]
        ja      .done
        jb      .apply
        cmp     ax, [irq_count]
        ja      .done
.apply:
        mov     al, cl
        call    kbd_effect
        inc     word [keys_applied]
        inc     si
        jmp     .next
.done:
        mov     [key_pos], si
.ret:
        ret

apply_pokes:                            ; scripted pokes with irq <= irq_count
        mov     ax, [dgroup_seg]
        or      ax, ax
        jz      .ret
        mov     es, ax
        mov     si, [poke_pos]
.next:
        cmp     si, [npokes]
        jae     .done
        mov     bx, si
        mov     cl, 3
        shl     bx, cl                  ; bx = si * POKE_REC
        mov     ax, [pokes + bx]
        mov     dx, [pokes + bx + 2]
        cmp     dx, [irq_count+2]
        ja      .done
        jb      .apply
        cmp     ax, [irq_count]
        ja      .done
.apply:
        mov     di, [pokes + bx + 4]
        mov     ax, [pokes + bx + 6]
        mov     [es:di], ax
        inc     word [pokes_applied]
        inc     si
        jmp     .next
.done:
        mov     [poke_pos], si
.ret:
        ret

kbd_effect:                             ; AL scancode, ES game DGROUP (as 1000:e567)
        mov     [es:KBD_LAST_RAW], al
        cmp     al, 80h
        jae     .bits
        mov     [es:KBD_LAST_MAKE], al
        mov     [es:KBD_LAST_MAKE2], al
.bits:
        mov     ah, al
        and     ah, 7Fh
        mov     bl, 80h                 ; space = fire
        cmp     ah, 39h
        je      .set
        mov     bl, 04h                 ; left
        cmp     ah, 4Bh
        je      .set
        mov     bl, 08h                 ; right
        cmp     ah, 4Dh
        je      .set
        mov     bl, 01h                 ; up
        cmp     ah, 48h
        je      .set
        mov     bl, 02h                 ; down
        cmp     ah, 50h
        je      .set
        ret
.set:
        test    al, 80h
        jnz     .clear
        or      [es:KBD_BITS], bl
        ret
.clear:
        not     bl
        and     [es:KBD_BITS], bl
        ret

maybe_snapshot:                         ; DS = CS, BP = wrapper frame
        cmp     byte [xms_ok], 0
        je      .ret
        cmp     word [dgroup_seg], 0
        je      .ret
        mov     ax, [irq_count]
        mov     dx, [irq_count+2]
        cmp     dx, [next_snap+2]
        jb      .vram
        ja      .due
        cmp     ax, [next_snap]
        jb      .vram
.due:
        mov     bx, [nsnaps]
        cmp     bx, [max_snaps]
        jae     .vram
        call    slot_base               ; DX:AX = XMS offset of slot BX
        mov     [xm_dst_off], ax
        mov     [xm_dst_off+2], dx
        mov     word [xm_len], 0
        mov     word [xm_len+2], 1      ; 64 KB
        mov     word [xm_src_handle], 0
        mov     word [xm_src_off], 0
        mov     ax, [dgroup_seg]
        mov     [xm_src_off+2], ax
        mov     ax, [xms_handle]
        mov     [xm_dst_handle], ax
        call    xms_move
        mov     bx, [nsnaps]            ; meta: DGROUP irq, VRAM irq = 0
        mov     cl, 3
        shl     bx, cl
        mov     ax, [irq_count]
        mov     [meta + bx], ax
        mov     ax, [irq_count+2]
        mov     [meta + bx + 2], ax
        mov     word [meta + bx + 4], 0
        mov     word [meta + bx + 6], 0
        inc     word [nsnaps]
        mov     al, [vga]
        mov     [vram_pending], al
        mov     ax, [interval]
        add     [next_snap], ax
        mov     ax, [interval+2]
        adc     [next_snap+2], ax
        ret                             ; VRAM at a later IRQ, never in the same one
.vram:
        cmp     byte [vram_pending], 0
        je      .ret
        mov     ax, [bp+20]
        cmp     ax, [game_cs]
        jne     .ret
        mov     ax, [bp+18]
        cmp     ax, VSYNC_FIRST
        jb      .ret
        cmp     ax, VSYNC_LAST
        ja      .ret
        call    snap_vram
        mov     byte [vram_pending], 0
.ret:
        ret

slot_base:                              ; BX slot -> DX:AX = BX * (20000h + SLOT_LOW)
        mov     ax, SLOT_LOW
        mul     bx
        add     dx, bx
        add     dx, bx
        ret

snap_vram:                              ; VRAM of the newest slot (VGA register readback)
        mov     dx, 3CEh
        in      al, dx
        mov     [gc_index], al
        mov     al, 4
        out     dx, al
        inc     dx
        in      al, dx
        mov     [gc_read_map], al
        dec     dx
        mov     al, 5
        out     dx, al
        inc     dx
        in      al, dx
        mov     [gc_mode], al
        dec     dx
        mov     al, 5                   ; read mode 0, write mode unchanged
        mov     ah, [gc_mode]
        and     ah, 0F7h
        out     dx, ax
        mov     bx, [nsnaps]
        dec     bx
        call    slot_base
        add     dx, 1                   ; skip the 64 KB DGROUP part
        mov     [xm_dst_off], ax
        mov     [xm_dst_off+2], dx
        mov     ax, [xms_handle]
        mov     [xm_dst_handle], ax
        mov     word [xm_src_handle], 0
        mov     word [xm_src_off], 0
        mov     word [xm_src_off+2], 0A000h
        mov     word [xm_len], VRAM_PLANE
        mov     word [xm_len+2], 0
        xor     cx, cx                  ; plane
.plane:
        mov     dx, 3CEh
        mov     al, 4
        mov     ah, cl
        out     dx, ax
        push    cx
        call    xms_move
        pop     cx
        add     word [xm_dst_off], VRAM_PLANE
        adc     word [xm_dst_off+2], 0
        inc     cx
        cmp     cx, 4
        jb      .plane
        mov     dx, 3CEh                ; restore the game's GC state
        mov     al, 4
        mov     ah, [gc_read_map]
        out     dx, ax
        mov     al, 5
        mov     ah, [gc_mode]
        out     dx, ax
        mov     al, [gc_index]
        out     dx, al
        mov     bx, [nsnaps]
        dec     bx
        mov     cl, 3
        shl     bx, cl
        mov     ax, [irq_count]
        mov     [meta + bx + 4], ax
        mov     ax, [irq_count+2]
        mov     [meta + bx + 6], ax
        ret

xms_move:                               ; DS = CS, xm_* filled in
        push    si
        mov     si, xm_len
        mov     ah, 0Bh
        call    far [xms_entry]
        pop     si
        ret

; ---- output ------------------------------------------------------------------------

write_snapshots:
        cmp     word [nsnaps], 0
        je      .ret
        mov     dx, snp_name
        call    create_file
        jc      .ret
        mov     ax, [nsnaps]
        mov     [snp_count], ax
        mov     ax, [interval]
        mov     [snp_interval], ax
        mov     ax, [interval+2]
        mov     [snp_interval+2], ax
        mov     al, [vga]
        and     ax, 1
        mov     [snp_flags], ax
        mov     dx, snp_header
        mov     cx, snp_header_end - snp_header
        call    write_chunk
        mov     dx, meta
        mov     cx, [nsnaps]
        shl     cx, 1
        shl     cx, 1
        shl     cx, 1
        call    write_chunk
        xor     bx, bx
.slot:
        cmp     bx, [nsnaps]
        jae     .close
        push    bx
        call    slot_base
        mov     [xfer_off], ax
        mov     [xfer_off+2], dx
        mov     word [xfer_left], 0
        mov     word [xfer_left+2], 1
        cmp     byte [vga], 0
        je      .dg_only
        add     word [xfer_left], VRAM_SIZE & 0FFFFh
        adc     word [xfer_left+2], VRAM_SIZE >> 16
.dg_only:
        call    xms_to_file
        pop     bx
        inc     bx
        jmp     .slot
.close:
        call    close_file
.ret:
        ret

xms_to_file:                            ; xfer_left bytes from XMS xfer_off -> file
.next:
        mov     ax, [xfer_left]
        or      ax, [xfer_left+2]
        jz      .ret
        mov     cx, 8000h
        cmp     word [xfer_left+2], 0
        jne     .chunk
        cmp     [xfer_left], cx
        jae     .chunk
        mov     cx, [xfer_left]
.chunk:
        mov     [xm_len], cx
        mov     word [xm_len+2], 0
        mov     ax, [xms_handle]
        mov     [xm_src_handle], ax
        mov     ax, [xfer_off]
        mov     [xm_src_off], ax
        mov     ax, [xfer_off+2]
        mov     [xm_src_off+2], ax
        mov     word [xm_dst_handle], 0
        mov     word [xm_dst_off], 0
        mov     ax, [bufseg]
        mov     [xm_dst_off+2], ax
        push    cx
        call    xms_move
        pop     cx
        push    cx
        push    ds
        mov     ds, [cs:bufseg]
        xor     dx, dx
        call    write_chunk
        pop     ds
        pop     cx
        add     [xfer_off], cx
        adc     word [xfer_off+2], 0
        sub     [xfer_left], cx
        sbb     word [xfer_left+2], 0
        jmp     .next
.ret:
        ret

; ---- helpers -------------------------------------------------------------------------

skip_spaces:
        cmp     byte [si], ' '
        jne     .out
        inc     si
        jmp     skip_spaces
.out:
        ret

parse_hex32:                            ; DS:SI -> DX:AX, up to 8 hex digits
        xor     ax, ax
        xor     dx, dx
        mov     cx, 8
.next:
        mov     bl, [si]
        call    hex_value
        jc      .out
        inc     si
        push    cx
        mov     cx, 4
.shift:
        shl     ax, 1
        rcl     dx, 1
        loop    .shift
        pop     cx
        or      al, bl
        loop    .next
.out:
        ret

hex_value:                              ; BL ascii -> BL value, CF if not hex
        cmp     bl, '0'
        jb      .bad
        cmp     bl, '9'
        jbe     .digit
        or      bl, 20h
        cmp     bl, 'a'
        jb      .bad
        cmp     bl, 'f'
        ja      .bad
        sub     bl, 'a' - 10
        clc
        ret
.digit:
        sub     bl, '0'
        clc
        ret
.bad:
        stc
        ret

parse_digit:                            ; DS:SI -> AL 0..9, CF if none
        mov     al, [si]
        sub     al, '0'
        cmp     al, 9
        ja      .none
        inc     si
        clc
        ret
.none:
        stc
        ret

create_file:                            ; DX name -> [cs:handle], CF on error
        mov     ah, 3Ch
        xor     cx, cx
        int     21h
        mov     [cs:handle], ax
        ret

write_chunk:                            ; DS:DX buffer, CX length
        push    ax
        push    bx
        mov     ah, 40h
        mov     bx, [cs:handle]
        int     21h
        pop     bx
        pop     ax
        ret

close_file:
        mov     ah, 3Eh
        mov     bx, [cs:handle]
        int     21h
        ret

; ---- data ----------------------------------------------------------------------------

game_name:  db      "GAME86.EXE", 0
hdr_name:   db      "PROBE.HDR", 0
dg_name:    db      "PROBE.DG", 0
snp_name:   db      "PROBE.SNP", 0
key_name:   db      "PROBE.KEY", 0
poke_name:  db      "PROBE.POK", 0

; command tail as llama.exe passes it (copied to the child PSP:80h)
tail:       db      0, 'l', 'm', 0Dh, 0, 0, 1, 0, 0, 0FFh, 0, 0, 0, 0
            times   128 - ($ - tail) db 0
fcb:        times   16 db 0

epb:        dw      0                   ; inherit environment
epb_tail:   dw      tail, 0
epb_fcb1:   dw      fcb, 0
epb_fcb2:   dw      fcb, 0

header:     db      "LTPB"
            dw      3
dgroup_seg: dw      0
ticks:      dd      0
ticks_seen: dd      0
snap_taken: db      0
exit_code:  db      0
exit_type:  db      0
exec_error: db      0
hdr_nsnaps: dw      0
xms_ok:     db      0
wrapped:    db      0
irq_count:  dd      0
keys_applied: dw    0
nkeys:      dw      0
pokes_applied: dw   0
npokes:     dw      0
header_end:

snp_header: db      "LTSN"
            dw      1
snp_count:  dw      0
snp_interval: dd    0
snp_flags:  dw      0
            dd      DG_SIZE
            dd      VRAM_SIZE
snp_header_end:

xm_len:         dd  0                   ; XMS move structure (function 0Bh)
xm_src_handle:  dw  0
xm_src_off:     dd  0
xm_dst_handle:  dw  0
xm_dst_off:     dd  0

; uninitialised data after the image (not stored in the .COM file)
            alignb  2
bss_start:
        absolute bss_start
old10:      resd    1
old21:      resd    1
game08:     resd    1
xms_entry:  resd    1
interval:   resd    1
next_snap:  resd    1
xfer_off:   resd    1
xfer_left:  resd    1
game_cs:    resw    1
bufseg:     resw    1
keyseg:     resw    1
handle:     resw    1
xms_handle: resw    1
max_snaps:  resw    1
nsnaps:     resw    1
key_pos:    resw    1
poke_pos:   resw    1
pic_mask:   resb    1
vga:        resb    1
in_game:    resb    1
depth:      resb    1
vram_pending: resb  1
gc_index:   resb    1
gc_read_map: resb   1
gc_mode:    resb    1
            alignb  2
meta:       resb    MAX_SNAPS * 8
pokes:      resb    MAX_POKES * POKE_REC
stack:      resb    1024
stack_top:
prog_end:
