EXTERN xPos : dword
EXTERN yPos : dword
EXTERN zPos : dword
EXTERN hookRetAddress : qword

.code
RecordPos proc
	movss xmm0, dword ptr[rsi + 90h]
	movss dword ptr [xPos], xmm0
	movss xmm0, dword ptr[rsi + 94h]
	movss dword ptr [yPos], xmm0
	movss xmm0, dword ptr[rsi + 98h]
	movss dword ptr [zPos], xmm0
	movups xmm0, xmmword ptr[rsi + 90h]
	jmp hookRetAddress
RecordPos endp
end