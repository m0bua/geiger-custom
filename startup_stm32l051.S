.syntax unified
.cpu cortex-m0plus
.thumb

.section .isr_vector,"a",%progbits
.global g_pfnVectors
g_pfnVectors:
    .word _estack
    .word Reset_Handler
    .word Default_Handler /* NMI */
    .word Default_Handler /* HardFault */
    .word Default_Handler
    .word Default_Handler
    .word Default_Handler
    .word Default_Handler
    .word Default_Handler
    .word Default_Handler
    .word Default_Handler
    .word Default_Handler /* SVCall */
    .word Default_Handler
    .word Default_Handler
    .word Default_Handler /* PendSV */
    .word SysTick_Handler
    .rept 7
    .word Default_Handler
    .endr
    .word EXTI4_15_IRQHandler
    .rept 24
    .word Default_Handler
    .endr

.section .text.Reset_Handler,"ax",%progbits
.global Reset_Handler
.type Reset_Handler,%function
.thumb_func
Reset_Handler:
    ldr r0, =_sidata
    ldr r1, =_sdata
    ldr r2, =_edata
1:
    cmp r1, r2
    bcs 2f
    ldr r3, [r0]
    str r3, [r1]
    adds r0, #4
    adds r1, #4
    b 1b
2:
    ldr r1, =_sbss
    ldr r2, =_ebss
    movs r3, #0
3:
    cmp r1, r2
    bcs 4f
    str r3, [r1]
    adds r1, #4
    b 3b
4:
    bl main
5:
    b 5b
.size Reset_Handler, .-Reset_Handler

.global Default_Handler
.type Default_Handler,%function
.thumb_func
Default_Handler:
    b Default_Handler
.size Default_Handler, .-Default_Handler

.weak SysTick_Handler
.thumb_set SysTick_Handler,Default_Handler

.section .note.GNU-stack,"",%progbits
