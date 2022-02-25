.text

.globl write
// parameters are just passed through
write:
    /* syscall write(int fd, const void *buf, size_t count) */
    mov     w8, #64     /* write is syscall #64 */
    svc     #0          /* invoke syscall */
    ret

.globl _start
_start:
    ldr w0, [sp]
    add x1, sp, #8 // this 8 would be a 4 in 32bit mode
    bl main
    b exit

.globl exit
exit:
    /* syscall exit(int status) */
    mov     w8, #93     /* exit is syscall #93 */
    svc     #0          /* invoke syscall */
    ret
