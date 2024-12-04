EXTERN xPos : dword
EXTERN yPos : dword
EXTERN zPos : dword
EXTERN hookRetAddress : qword
EXTERN loggingAddress : qword

.code
recordPos proc
	movss xmm0, dword ptr[rsi + 90h]
	movss dword ptr [xPos], xmm0
	movss xmm1, dword ptr[rsi + 94h]
	movss dword ptr [yPos], xmm1
	movss xmm2, dword ptr[rsi + 98h]
	movss dword ptr [zPos], xmm2
	movups xmm0, xmmword ptr[rsi + 90h]
	mov rax, loggingAddress
	call rax
	jmp hookRetAddress
recordPos endp
end