"""Rebuilds every original text from data/strings.dat and compares it with game86 DGROUP.

Usage: strings_roundtrip.py <generated>/data/strings.dat <generated>/exe/game86.unpacked
"""
import struct,sys
d=open(sys.argv[1],'rb').read(); img=open(sys.argv[2],'rb').read(); B=(0x3559-0x1000)*16
p=4; ver,n=struct.unpack_from('<HH',d,p); p+=4
T=[struct.unpack_from('<3H',d,p+i*6) for i in range(n)]; p+=n*6
nt=struct.unpack_from('<H',d,p)[0]; p+=2; titles=[struct.unpack_from('<hH',d,p+i*4) for i in range(nt)]; p+=nt*4
nm=struct.unpack_from('<H',d,p)[0]; p+=2; msgs=[]
for i in range(nm):
    o,l=struct.unpack_from('<HH',d,p); msgs.append((o,d[p+4:p+4+l])); p+=4+l
ol=struct.unpack_from('<H',d,p)[0]; p+=2; ops=d[p:p+ol]; assert p+ol==len(d)
bad=0
for ds,first,ln in T:
    q=first; out=bytearray()
    while True:
        op=ops[q]; q+=1
        if op==0: out+=b'\\e'; break
        if op in (1,2):
            l=struct.unpack_from('<H',ops,q)[0]; q+=2
            out+= (b'\\q'+ops[q:q+l]+b'\0') if op==2 else ops[q:q+l]; q+=l
        elif op==3: out+=b'\\r'
        elif op==4: v=struct.unpack_from('<H',ops,q)[0]; q+=2; out+=b'\\s'+bytes([v>>8,v&255])
        elif op in (5,6,7,8): out+=b'\\'+b'udli'[op-5:op-4]
        elif op==9: out+=b'\\g'+ops[q:q+2]; q+=2
        elif op==10: out+=b'\\c'+ops[q:q+1]; q+=1
        elif op==11: out+=b'\\'+ops[q:q+1]; q+=1
    assert q==first+ln
    if img[B+ds:B+ds+len(out)]!=bytes(out): bad+=1; print('mismatch',hex(ds))
for o,m in msgs:
    if img[B+o:B+o+len(m)+1]!=m+b'$': bad+=1; print('msg mismatch',hex(o))
print(n,'texts',nt,'titles',titles,nm,'messages; mismatches',bad)
sys.exit(1 if bad else 0)
