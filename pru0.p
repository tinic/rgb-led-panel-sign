.origin 0
.entrypoint START

#define AM33XX
#define PRU0_PRU1_INTERRUPT     17
#define PRU1_PRU0_INTERRUPT     18
#define PRU0_ARM_INTERRUPT      19
#define PRU1_ARM_INTERRUPT      20
#define ARM_PRU0_INTERRUPT      21
#define ARM_PRU1_INTERRUPT      22

#define CONST_PRUDRAM   C24
#define CONST_SHAREDRAM C28
#define CONST_L3RAM     C30
#define CONST_DDR       C31

#define CTBIR_0         0x22020
#define CTBIR_1         0x22024
#define CTPPR_0         0x22028
#define CTPPR_1         0x2202C

#define GPIO0           0x44E07000
#define GPIO1           0x4804c000
#define GPIO2           0x481AC000
#define GPIO3           0x481AE000

#define GPIO_DATAOUT    0x13C

// -----------------------------------------------------

#define CLK  7
#define STB 15
#define OE  14

.struct VarStruct
    .u32    tmp0
    .u32    tmp1
    .u32    const0
    .u8     y
    .u8     p
    .u16    x
    .u32    ram_addr
    .u32    gpio2_dataout
    .u32    src_addr
    .u32    gamma
.ends
.assign VarStruct, r0, r7, Vars

.struct ParamsStruct
    .u32    ddr_addr
    .u32    ready
    .u8     pwmlength
    .u8     nledpanels
    .u8     ntotalrowpixels
    .u8     panelheightdiv2
    .u8     panelheightdiv2p1
    .u8     dummy0
    .u16    dummy1
.ends
.assign ParamsStruct, r8, r11, Params

START:
    LBCO    Vars.tmp0, C4, 4, 4
    CLR     Vars.tmp0, Vars.tmp0, 4
    SBCO	Vars.tmp0, C4, 4, 4

    MOV     Vars.tmp0, 0x00000120
    MOV     Vars.tmp1, CTPPR_0
    SBBO    Vars.tmp0, Vars.tmp1, 0, 4

    MOV     Vars.tmp0, 0x00100000
    MOV     Vars.tmp1, CTPPR_1
    SBBO    Vars.tmp0, Vars.tmp1, 0, 4

    MOV     Vars.tmp0, #0x01
    SBCO    Vars.tmp0, CONST_PRUDRAM, 4, 4


    MOV     Vars.const0, #0xFFFFFF00
    MOV     Vars.gpio2_dataout, GPIO2 + GPIO_DATAOUT

STANDBY_LOOP:
    LBCO    r8, CONST_PRUDRAM, 0, SIZE(ParamsStruct)
    QBEQ    STANDBY_LOOP, Params.ddr_addr, #0x00
    QBEQ    EXIT, Params.ddr_addr, #0x01

    MOV     Vars.y, #0x00

ROW_LOOP:

    MOV     Vars.ram_addr, #0x00001000

.macro  COPY192
.mparam dst, src
    LBBO    r13, src, #0x00, #0x40
    ADD     src, src, #0x40
    SBCO    r13, CONST_PRUDRAM, dst, #0x40
    ADD     dst, dst, #0x40

    LBBO    r13, src, #0x00, #0x40
    ADD     src, src, #0x40
    SBCO    r13, CONST_PRUDRAM, dst, #0x40
    ADD     dst, dst, #0x40

    LBBO    r13, src, #0x00, #0x40
    ADD     src, src, #0x40
    SBCO    r13, CONST_PRUDRAM, dst, #0x40
    ADD     dst, dst, #0x40
.endm

    // Stream row data from DDR memory to local PRU memory
    MOV     Vars.tmp0, #0x00
COPY_LOOP:
    COPY192 Vars.ram_addr, Params.ddr_addr
    ADD     Vars.tmp0, Vars.tmp0, #0x01
    QBNE    COPY_LOOP, Vars.tmp0, Params.nledpanels

    MOV     Vars.ram_addr, #0x00001000

    // display off
    SET     r30, r30, OE

    // set address on LED panel using non-PRU GPIO pins
    LBBO    Vars.tmp0, Vars.gpio2_dataout, 0x00, 0x04
    MOV     Vars.tmp1, #0xFFFFFFC3;
    AND     Vars.tmp0, Vars.tmp0, Vars.tmp1
    LSL     Vars.tmp1, Vars.y, #0x02
    OR      Vars.tmp0, Vars.tmp0, Vars.tmp1
    SBBO    Vars.tmp0, Vars.gpio2_dataout, 0x00, 0x04

    MOV     Vars.p, #0x00
PWM_LOOP:

    // clear latch
    CLR     r30, r30, STB

    ADD     Vars.tmp0, Vars.p, SIZE(ParamsStruct)
    MOV     Vars.gamma, #0x00
    LBCO    Vars.gamma.b0, CONST_PRUDRAM, Vars.tmp0, #1

    MOV     Vars.x, #0x00
    MOV     Vars.src_addr, Vars.ram_addr

COL_LOOP:

.macro  CHECKGAMMA
    // Switch off early based on gamma value
    QBGE    skip_display_off, Vars.x, Vars.gamma
    SET     r30, r30, OE
skip_display_off:
.endm

.macro  SETB
.mparam bit, byte
    QBGE    skip_set_bit, byte, Vars.p
    SET     r30, r30, bit
skip_set_bit:
.endm

.macro  SETP
.mparam reg0, reg1
    CHECKGAMMA
    ADD     Vars.x, Vars.x, #0x1

    // Load 6 bytes, 2 pixels, 1 top pixel, 1 bottom pixel
    LBBO    reg0, Vars.src_addr, #0x00, #0x06
    ADD     Vars.src_addr, Vars.src_addr, #0x06

    AND     r30, r30, Vars.const0 // also clears CLK
    SETB    0, reg0.b0
    SETB    2, reg0.b1
    SETB    4, reg0.b2
    SETB    1, reg0.b3
    SETB    3, reg1.b0
    SETB    5, reg1.b1
    SET     r30, r30, CLK // needs to be delayed
.endm

    SETP    r13, r14
    SETP    r15, r16
    SETP    r17, r18
    SETP    r19, r20
    SETP    r21, r22
    SETP    r23, r24
    SETP    r25, r26
    SETP    r27, r28

    SETP    r13, r14
    SETP    r15, r16
    SETP    r17, r18
    SETP    r19, r20
    SETP    r21, r22
    SETP    r23, r24
    SETP    r25, r26
    SETP    r27, r28

    QBNE    COL_LOOP, Vars.x, Params.ntotalrowpixels

    // set latch
    SET     r30, r30, STB
    // display on, might already be on
    CLR     r30, r30, OE

    ADD     Vars.p, Vars.p, #0x01
    QBNE    PWM_LOOP, Vars.p, Params.pwmlength

    SET     r30, r30, OE

    ADD     Vars.y, Vars.y, #0x02
    QBEQ    ILC_LOOP, Vars.y, Params.panelheightdiv2
    QBNE    ROW_LOOP, Vars.y, Params.panelheightdiv2p1
    QBA     STANDBY_LOOP

ILC_LOOP:
    MOV     Vars.y, #1
    JMP     ROW_LOOP

EXIT:
    SET     r30, r30, OE
    MOV R31.b0, PRU0_ARM_INTERRUPT+16
    HALT
