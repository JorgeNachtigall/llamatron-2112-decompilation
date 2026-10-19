; PROBE.COM - DOS-side helper for DOSBox-X reference runs (docs/PLAN.md §3.3).
; Test-oracle code only, never part of src/. Our own code; it contains nothing from
; the original. Assemble: nasm -f bin -o PROBE.COM probe.asm
;
;   PROBE tttttttt [v [f]]
;     tttttttt  BIOS tick count to pin (8 hex digits) = the game's RNG seed source
;     v         VGA flag for PSP:86h (0/1, default 1)
;     f         flash option for PSP:85h (0 none, 1 'P', 2 'Y'; default 0)
;
; What it does:
;  1. Masks IRQ0 at the PIC and writes tttttttt to 0040:006C, so the BIOS tick word
;     stays constant while game86 seeds (1000:5f6b) and shuffles its random table
;     (1000:007b..00ab, which re-reads the tick on every mixer call).
;  2. Hooks INT 10h. game86's first INT 10h AX=000Dh (video_set_mode from 1000:5960)
;     comes right after the shuffle: the hook copies the caller's DS (DGROUP, 64 KB)
;     into a buffer, records the tick word, unmasks IRQ0 and chains to the BIOS.
;  3. EXECs game86.exe with the launcher's command-tail contract (llama.exe
;     1000:0103; re-notes §1): PSP:80h=0, 81h='l', 82h='m', 83h=CR, 84h=0 (8086 build),
;     85h=flash, 86h=VGA, 88h=0 (keyboard), 89h=FFh (no sound driver).
;  4. After the game exits, restores the vectors and writes PROBE.HDR (header below)
;     and PROBE.DG (the DGROUP snapshot) to the current directory.
;
; PROBE.HDR (little endian):
;   +0 'LTPB'  +4 u16 version (1)  +6 u16 DGROUP segment seen by the hook
;   +8 u32 tick pinned  +12 u32 tick word at snapshot time  +16 u8 snapshot taken
;   +17 u8 game exit code  +18 u8 exit type (INT 21h/4Dh AH; FFh = EXEC failed)
;   +19 u8 EXEC error code

        cpu     8086
        org     100h

start:
        mov     sp, stack_top
        cld
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

        ; give memory back to DOS: keep the program, its .bss and the stack
        mov     bx, prog_end + 15
        mov     cl, 4
        shr     bx, cl
        mov     ah, 4Ah                 ; ES = our PSP
        int     21h
        jc      fatal
        mov     ah, 48h                 ; 64 KB snapshot buffer
        mov     bx, 1000h
        int     21h
        jc      fatal
        mov     [bufseg], ax

        mov     ax, 3510h               ; hook INT 10h
        int     21h
        mov     [old10], bx
        mov     [old10+2], es
        mov     dx, int10_hook
        mov     ax, 2510h
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
        mov     ax, 4B00h
        int     21h
        cli                             ; EXEC may clobber everything but CS:IP
        mov     bx, cs
        mov     ss, bx
        mov     sp, stack_top
        sti
        mov     ds, bx
        mov     es, bx
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
        lds     dx, [old10]
        mov     ax, 2510h
        int     21h
        pop     ds

        mov     dx, hdr_name            ; PROBE.HDR
        call    create_file
        jc      fatal
        mov     dx, header
        mov     cx, header_end - header
        call    write_chunk
        call    close_file

        cmp     byte [snap_taken], 0
        je      .done
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
.done:
        mov     ax, 4C00h
        int     21h

fatal:
        mov     ax, 4C01h
        int     21h

; ---- INT 10h hook ------------------------------------------------------------

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

; ---- helpers -------------------------------------------------------------------

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
        mov     ah, 40h
        mov     bx, [cs:handle]
        int     21h
        ret

close_file:
        mov     ah, 3Eh
        mov     bx, [cs:handle]
        int     21h
        ret

; ---- data ----------------------------------------------------------------------

game_name:  db      "GAME86.EXE", 0
hdr_name:   db      "PROBE.HDR", 0
dg_name:    db      "PROBE.DG", 0

; command tail as llama.exe passes it (copied to the child PSP:80h)
tail:       db      0, 'l', 'm', 0Dh, 0, 0, 1, 0, 0, 0FFh, 0, 0, 0, 0
            times   128 - ($ - tail) db 0
fcb:        times   16 db 0

epb:        dw      0                   ; inherit environment
epb_tail:   dw      tail, 0
epb_fcb1:   dw      fcb, 0
epb_fcb2:   dw      fcb, 0

header:     db      "LTPB"
            dw      1
dgroup_seg: dw      0
ticks:      dd      0
ticks_seen: dd      0
snap_taken: db      0
exit_code:  db      0
exit_type:  db      0
exec_error: db      0
header_end:

; uninitialised data after the image (not stored in the .COM file)
            alignb  2
bss_start:
        absolute bss_start
old10:      resd    1
bufseg:     resw    1
handle:     resw    1
pic_mask:   resb    1
            alignb  2
stack:      resb    512
stack_top:
prog_end:
