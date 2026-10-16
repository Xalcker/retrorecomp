; neobios: BIOS de reemplazo del Neo Geo AES, propia de NEORecomp (licencia MIT, como el resto del repo).
;
; Hecha desde cero a partir de la interfaz documentada en la wiki de NeoGeoDev (las llamadas de $C00402, las
; variables de la BIOS en la RAM, la cabecera del programa, USER/PLAYER_START, MESS_OUT, FIX_CLEAR, LSP_1st,
; CARD) y de lo que se ve desde afuera con la BIOS original (qué escribe en los registros y cuándo). No traduce
; ni copia el código de SNK ni el de otras BIOS. Se ensambla con tools/neobios/asm68k.py:
;
;   python3 tools/neobios/asm68k.py tools/neobios/neobios.s build/neobios.bin --swap
;
; Lo que hace (AES): arranca la consola, deja el Z80 del cartucho en su estado inicial, llena los DIP por
; software con los valores por omisión del juego, llama a USER con USER_REQUEST 0 (inicio), 1 (la pantalla de
; arranque del juego, si la hace él) y 2 (demo y partida, para siempre), y atiende las llamadas que hacen los
; juegos: SYSTEM_IO (mandos, Start y PLAYER_START), SYSTEM_INT1, MESS_OUT, FIX_CLEAR, LSP_1st... Lo que es
; del MVS (créditos, calendario, cambiar de ranura, HOW TO PLAY) no hace nada. La tarjeta de memoria (CARD y
; CARD_ERROR) usa el formato que deja la original, medido desde afuera con tools/neocard: una tarjeta escrita
; por una se lee con la otra. Todavía no: la animación de la pantalla de SNK (con el byte $114 en 0 se salta).

; --- Registros ---
REG_P1CNT       equ     $300000
REG_DIPSW       equ     $300001         ; escribir: patear al perro guardián
REG_SOUND       equ     $320000
REG_P2CNT       equ     $340000
REG_STATUS_B    equ     $380000         ; bits 4-5: tarjeta (0 si está), bit 6: protegida
REG_CRDBANK     equ     $380011
REG_NOSHADOW    equ     $3A0001
REG_SWPBIOS     equ     $3A0003
REG_SWPROM      equ     $3A0013
REG_PALBANK1    equ     $3A000F
REG_PALBANK0    equ     $3A001F
REG_CRDUNLOCK1  equ     $3A0005
REG_CRDLOCK1    equ     $3A0015
REG_CRDLOCK2    equ     $3A0007
REG_CRDUNLOCK2  equ     $3A0017
REG_VRAMADDR    equ     $3C0000
REG_VRAMRW      equ     $3C0002
REG_VRAMMOD     equ     $3C0004
REG_LSPCMODE    equ     $3C0006
REG_IRQACK      equ     $3C000C
PALETTES        equ     $400000
BACKDROP        equ     $401FFE
CARD_BASE       equ     $800001         ; tarjeta de 8 bits: el byte n en CARD_BASE + 2n

; --- La cabecera del juego ---
CART_USER       equ     $000122
CART_PSTART     equ     $000128
CART_EYECATCH   equ     $000114
CART_DIP_JP     equ     $000116         ; tres punteros: Japón, EE. UU., Europa

; --- Variables de la BIOS (documentadas) ---
BIOS_SYSTEM_MODE        equ     $10FD80
BIOS_MVS_FLAG           equ     $10FD82
BIOS_COUNTRY_CODE       equ     $10FD83
BIOS_GAME_DIP           equ     $10FD84
BIOS_P1STATUS           equ     $10FD94
BIOS_P2STATUS           equ     $10FD9A
BIOS_STATCURNT          equ     $10FDAC
BIOS_STATCHANGE         equ     $10FDAD
BIOS_USER_REQUEST       equ     $10FDAE
BIOS_USER_MODE          equ     $10FDAF
BIOS_START_FLAG         equ     $10FDB4
BIOS_PLAYER_MOD1        equ     $10FDB6
BIOS_MESS_POINT         equ     $10FDBE
BIOS_MESS_BUSY          equ     $10FDC2
BIOS_CARD_COMMAND       equ     $10FDC4
BIOS_CARD_ANSWER        equ     $10FDC6
BIOS_CARD_START         equ     $10FDC8
BIOS_CARD_SIZE          equ     $10FDCC
BIOS_CARD_FCB           equ     $10FDCE
BIOS_CARD_SUB           equ     $10FDD0
BIOS_STATCURNT_RAW      equ     $10FEDC
BIOS_STATCHANGE_RAW     equ     $10FEDD
BIOS_FRAME_COUNTER      equ     $10FE88
BIOS_INT1_SKIP          equ     $10FEE3
BIOS_INT1_FRAME_COUNTER equ     $10FEE4
BIOS_MESS_BUFFER        equ     $10FF00
; No documentada; se ve desde afuera con la BIOS original: quien quiere esperar un cuadro la pone en 1 y el
; VBLANK de la BIOS la vuelve a 0 (KOF '98 la usa en su pantalla de arranque, con el VBLANK en la BIOS)
BIOS_VBL_FLAG           equ     $10FE8C

; --- Variables propias: en $10FE90-$10FEBE, dentro de la zona de la BIOS y sin uso documentado. Ningún juego de
; roms/ escribe ahí con la BIOS original (KOF '98, por ejemplo, usa $10FC00-$10FC7B como RAM propia) ---
NB_STATE        equ     $10FE90         ; .b qué viene después de SYSTEM_RETURN: 0 tras el inicio, 1 tras USER 1, 2 tras la demo
NB_FRAMES       equ     $10FE92         ; .w cuadros contados por el VBLANK de la BIOS
NB_STAT_PREV    equ     $10FE94         ; .b Start/Select del cuadro anterior
NB_VRAM         equ     $10FE96         ; .w MESS_OUT: dirección de la VRAM
NB_INC          equ     $10FE98         ; .w MESS_OUT: incremento
NB_FMT          equ     $10FE9A         ; .w MESS_OUT: formato (bit 9: palabras; bit 8: con tamaño)
NB_FMT_ARG      equ     $10FE9C         ; .w MESS_OUT: byte alto y código de fin o tamaño
NB_DATA         equ     $10FE9E         ; .l MESS_OUT: por dónde iban los datos
NB_CARD_N       equ     $10FEA2         ; .w CARD: bloques de 64 bytes de la tarjeta
NB_CARD_FATB    equ     $10FEA4         ; .w CARD: bloques de cada copia de la FAT
NB_CARD_DATA    equ     $10FEA6         ; .w CARD: el primer bloque de datos
; CARD_ERROR guarda lo que tapa del juego en la zona de la BIOS (la original también la usa para eso)
NB_FIX_SAVE     equ     $10F300         ; 40 columnas × 28 filas de la capa fija: hasta $10FBBF
NB_PAL_SAVE     equ     $10FBC0         ; .l los colores 1 y 2 de la paleta 0
STACK_TOP       equ     $10F300

; ==========================================================================================================
        org     $C00000
vectors:
        dc.l    STACK_TOP               ;  0 SSP
        dc.l    $C00402                 ;  1 reset
        dc.l    $C00408                 ;  2 error de bus
        dc.l    $C0040E                 ;  3 error de dirección
        dc.l    $C00414                 ;  4 instrucción ilegal
        dc.l    $C00426                 ;  5 división entre cero
        dc.l    $C00426                 ;  6 CHK
        dc.l    $C00426                 ;  7 TRAPV
        dc.l    $C0041A                 ;  8 privilegio
        dc.l    $C00420                 ;  9 traza
        dc.l    $C00426                 ; 10 línea A
        dc.l    $C00426                 ; 11 línea F
        dc.l    $C00426,$C00426,$C00426,$C00426,$C00426,$C00426,$C00426,$C00426,$C00426,$C00426,$C00426,$C00426
        dc.l    $C00432                 ; 24 espuria
        dc.l    bios_vblank             ; 25 nivel 1: VBLANK
        dc.l    bios_timer              ; 26 nivel 2: temporizador
        dc.l    bios_cold               ; 27 nivel 3: pendiente tras encender
        dc.l    $C00426,$C00426,$C00426,$C00426     ; 28-31
        dc.l    $C00426,$C00426,$C00426,$C00426,$C00426,$C00426,$C00426,$C00426     ; 32-39 TRAP
        dc.l    $C00426,$C00426,$C00426,$C00426,$C00426,$C00426,$C00426,$C00426     ; 40-47
        dc.l    $C00426,$C00426,$C00426,$C00426,$C00426,$C00426,$C00426,$C00426     ; 48-55
        dc.l    $C00426,$C00426,$C00426,$C00426,$C00426,$C00426,$C00426,$C00426     ; 56-63

        org     $C00100
        dc.b    "NEORECOMP BIOS 1",0    ; firma: así la reconoce el runtime (no por el SHA1, que cambia)
        dc.b    "BIOS propia de NEORecomp (MIT). No contiene codigo de SNK.",0

        org     $C00400
        dc.b    $00                     ; sistema: AES
        dc.b    $02                     ; región: Europa (como neo-epo.bin, la de exportación)

; --- Tabla de saltos: 6 bytes por entrada, en las direcciones que llaman los juegos ---
        org     $C00402
        jmp     reset                   ; $C00402
        jmp     ex_halt                 ; $C00408 error de bus
        jmp     ex_halt                 ; $C0040E error de dirección
        jmp     ex_halt                 ; $C00414 instrucción ilegal
        jmp     ex_halt                 ; $C0041A privilegio
        jmp     ex_halt                 ; $C00420 traza
        jmp     reset                   ; $C00426 los demás: reinicio
        jmp     do_rte                  ; $C0042C
        jmp     do_rte                  ; $C00432 espuria
        jmp     system_int1             ; $C00438 SYSTEM_INT1
        jmp     system_int2             ; $C0043E SYSTEM_INT2
        jmp     system_return           ; $C00444 SYSTEM_RETURN
        jmp     system_io               ; $C0044A SYSTEM_IO
        jmp     do_rts                  ; $C00450 CREDIT_CHECK (AES: siempre alcanza)
        jmp     do_rts                  ; $C00456 CREDIT_DOWN
        jmp     do_rts                  ; $C0045C READ_CALENDAR (MVS)
        jmp     do_rts                  ; $C00462 SETUP_CALENDAR (MVS)
        jmp     card                    ; $C00468 CARD
        jmp     card_error              ; $C0046E CARD_ERROR
        jmp     do_rts                  ; $C00474 HOW_TO_PLAY (MVS)
        jmp     ex_halt                 ; $C0047A suma de control y se queda
        jmp     do_rts                  ; $C00480
        jmp     do_rts                  ; $C00486
        jmp     do_rts                  ; $C0048C
        jmp     do_rts                  ; $C00492
        jmp     do_rts                  ; $C00498
        jmp     do_rts                  ; $C0049E
        jmp     do_rts                  ; $C004A4
        jmp     do_rts                  ; $C004AA
        jmp     do_rts                  ; $C004B0
        jmp     do_rts                  ; $C004B6
        jmp     do_rts                  ; $C004BC
        jmp     fix_clear               ; $C004C2 FIX_CLEAR
        jmp     lsp_1st                 ; $C004C8 LSP_1st
        jmp     mess_out                ; $C004CE MESS_OUT
        jmp     controller_setup        ; $C004D4 CONTROLLER_SETUP
        jmp     system_int2             ; $C004DA

; ==========================================================================================================
        org     $C00600
do_rts:
        rts
do_rte:
        rte

; Una excepción que no se puede atender: se queda (sin patear al perro guardián, que reinicia la consola)
ex_halt:
        move    #$2700,sr
.loop:  bra.s   .loop

; --- Interrupciones mientras están los vectores de la BIOS ---
bios_vblank:
        move.w  #4,REG_IRQACK
        move.b  d0,REG_DIPSW
        addq.w  #1,NB_FRAMES
        clr.b   BIOS_VBL_FLAG
        rte
bios_timer:
        move.w  #2,REG_IRQACK
        rte
bios_cold:
        move.w  #1,REG_IRQACK
        rte

; Espera d0 cuadros con los vectores de la BIOS (las interrupciones habilitadas)
wait_frames:
        move.w  NB_FRAMES,d1
        add.w   d0,d1
        move    #$2000,sr
.w:     move.b  d0,REG_DIPSW
        cmp.w   NB_FRAMES,d1
        bne.s   .w
        move    #$2700,sr
        rts

; ==========================================================================================================
; Arranque
reset:
        move    #$2700,sr
        lea     STACK_TOP,sp
        move.b  d0,REG_DIPSW
        move.w  #7,REG_IRQACK
        move.w  #$4000,REG_LSPCMODE     ; autoanimación cada 64 cuadros, sin temporizador
        move.b  d0,REG_SWPBIOS          ; los vectores de la BIOS
        move.b  d0,REG_PALBANK0
        move.b  d0,REG_NOSHADOW
        ; La RAM del 68000 en cero (patear al perro guardián cada tanto)
        lea     $100000,a0
        move.w  #$0FFF,d1
        moveq   #0,d0
.ram:   move.l  d0,(a0)+
        move.l  d0,(a0)+
        move.l  d0,(a0)+
        move.l  d0,(a0)+
        move.b  d0,REG_DIPSW
        dbf     d1,.ram
        ; Las dos paletas en negro
        move.b  d0,REG_PALBANK1
        bsr.w   clear_palette
        move.b  d0,REG_PALBANK0
        bsr.w   clear_palette
        ; La VRAM: capa fija y sprites
        bsr.w   fix_clear
        bsr.w   lsp_1st
        ; Variables de la BIOS
        move.b  #0,BIOS_SYSTEM_MODE
        move.b  #0,BIOS_MVS_FLAG
        move.b  $C00401,BIOS_COUNTRY_CODE
        move.l  #BIOS_MESS_BUFFER,BIOS_MESS_POINT
        bsr.w   controller_setup
        bsr.w   load_dips
        ; El programa de sonido del cartucho: que empiece de cero (lo mismo que hace la original)
        bsr.w   wait_one
        move.b  #$03,REG_SOUND
        moveq   #8,d0
        bsr.w   wait_frames
        ; USER 0: el juego inicializa lo suyo
        move.b  #0,NB_STATE
        move.b  #0,BIOS_USER_MODE
        moveq   #0,d0
        bra.w   call_user

wait_one:
        moveq   #1,d0
        bra.w   wait_frames

clear_palette:
        lea     PALETTES,a0
        move.w  #$0FFF,d1
.p:     move.w  #$8000,(a0)+            ; negro (con el bit de oscurecido)
        dbf     d1,.p
        rts

; BIOS_GAME_DIP con los valores por omisión del juego (la tabla de su región en la cabecera): los 6 bytes de
; los ajustes especiales tal cual y el nibble alto de cada ajuste simple
load_dips:
        moveq   #0,d0
        move.b  BIOS_COUNTRY_CODE,d0
        cmpi.b  #2,d0
        bls.s   .ok
        moveq   #2,d0
.ok:    lsl.w   #2,d0
        lea     CART_DIP_JP,a0
        movea.l 0(a0,d0.w),a0
        move.l  a0,d0
        beq.s   .done                   ; sin tabla
        lea     16(a0),a0               ; después del nombre
        lea     BIOS_GAME_DIP,a1
        moveq   #5,d1
.sp:    move.b  (a0)+,(a1)+
        dbf     d1,.sp
        moveq   #9,d1
.si:    move.b  (a0)+,d0
        lsr.b   #4,d0
        move.b  d0,(a1)+
        dbf     d1,.si
.done:  rts

; Llama a USER con USER_REQUEST d0: los vectores del cartucho y un salto (vuelve por SYSTEM_RETURN)
call_user:
        move.b  d0,BIOS_USER_REQUEST
        move    #$2700,sr
        lea     STACK_TOP,sp
        move.w  #7,REG_IRQACK
        move.b  d0,REG_SWPROM
        cmpi.b  #2,d0
        bne.s   .go
        move.b  #$80,BIOS_SYSTEM_MODE   ; demo y partida: el VBLANK es del juego
        move    #$2000,sr
.go:    jmp     CART_USER

; SYSTEM_RETURN: lo que sigue según de dónde se volvió
system_return:
        move    #$2700,sr
        lea     STACK_TOP,sp
        move.b  #0,BIOS_SYSTEM_MODE
        move.b  NB_STATE,d0
        bne.s   .not_init
        ; Tras el inicio: la pantalla de arranque del juego si la hace él ($114 = 1)
        move.b  #1,NB_STATE
        cmpi.b  #1,CART_EYECATCH
        bne.s   .demo
        moveq   #1,d0
        bra.w   call_user
.not_init:
.demo:  ; Demo y partida (en el AES no se pasa de aquí: al terminar se vuelve a empezar)
        move.b  #2,NB_STATE
        move.b  d0,REG_SWPBIOS
        move.b  #$03,REG_SOUND
        moveq   #2,d0
        bsr.w   wait_frames
        moveq   #2,d0
        bra.w   call_user

; ==========================================================================================================
; SYSTEM_INT1: el VBLANK de la BIOS para los juegos (cuando BIOS_SYSTEM_MODE tiene el bit 7 en 0)
system_int1:
        movem.l d0-d7/a0-a6,-(sp)
        move.w  #4,REG_IRQACK
        move.b  d0,REG_DIPSW
        addq.b  #1,BIOS_INT1_FRAME_COUNTER
        clr.b   BIOS_VBL_FLAG
        tst.b   BIOS_INT1_SKIP
        bne.s   .out
        bsr.w   system_io
        bsr.w   mess_out
.out:   movem.l (sp)+,d0-d7/a0-a6
        rte

; SYSTEM_INT2 (y $C004DA): reconoce la interrupción del temporizador
system_int2:
        move.w  #2,REG_IRQACK
        rte

; ==========================================================================================================
; SYSTEM_IO: lee los mandos y Start/Select, y llama a PLAYER_START (no guarda los registros)
system_io:
        addq.l  #1,BIOS_FRAME_COUNTER
        lea     BIOS_P1STATUS,a0
        move.b  REG_P1CNT,d0
        bsr.w   read_pad
        lea     BIOS_P2STATUS,a0
        move.b  REG_P2CNT,d0
        bsr.w   read_pad
        ; Start y Select (lógica positiva): Start P1 bit 0, Select P1 bit 1, Start P2 bit 2, Select P2 bit 3
        move.b  REG_STATUS_B,d0
        not.b   d0
        andi.b  #$0F,d0
        move.b  NB_STAT_PREV,d1
        move.b  d0,NB_STAT_PREV
        move.b  d0,BIOS_STATCURNT
        move.b  d0,BIOS_STATCURNT_RAW
        not.b   d1
        and.b   d0,d1
        move.b  d1,BIOS_STATCHANGE
        move.b  d1,BIOS_STATCHANGE_RAW
        ; Start recién apretado por un jugador que no está jugando: PLAYER_START
        cmpi.b  #2,BIOS_USER_REQUEST
        bne.s   .done
        moveq   #0,d2                   ; START_FLAG
        btst    #0,d1
        beq.s   .p2
        cmpi.b  #1,BIOS_PLAYER_MOD1
        beq.s   .p2
        bset    #0,d2
.p2:    btst    #2,d1
        beq.s   .call
        cmpi.b  #1,BIOS_PLAYER_MOD1+1
        beq.s   .call
        bset    #1,d2
.call:  tst.b   d2
        beq.s   .done
        move.b  d2,BIOS_START_FLAG
        jsr     CART_PSTART
.done:  rts

; Un mando: a0 = su bloque (STATUS, PREVIOUS, CURRENT, CHANGE, REPEAT, TIMER), d0 = el registro (activo en 0)
read_pad:
        not.b   d0
        move.b  2(a0),d1                ; el de antes
        move.b  d1,1(a0)
        move.b  d0,2(a0)
        not.b   d1
        and.b   d0,d1                   ; recién apretados
        move.b  d1,3(a0)
        ; Repetición: el flanco, y cada 8 cuadros después de 16 con algo apretado
        tst.b   d0
        bne.s   .held
        clr.b   4(a0)
        move.b  #16,5(a0)
        rts
.held:  tst.b   d1
        beq.s   .rep
        move.b  d1,4(a0)
        move.b  #16,5(a0)
        rts
.rep:   clr.b   4(a0)
        subq.b  #1,5(a0)
        bne.s   .r
        move.b  d0,4(a0)
        move.b  #8,5(a0)
.r:     rts

; CONTROLLER_SETUP: dos mandos normales
controller_setup:
        move.b  #1,BIOS_P1STATUS
        move.b  #1,BIOS_P2STATUS
        rts

; ==========================================================================================================
; CARD: la tarjeta de memoria. BIOS_CARD_COMMAND: 0 formatear, 1 buscar (BIOS_CARD_SUB: las partidas del juego
; BIOS_CARD_FCB, un bit cada una), 2 cargar, 3 guardar, 4 borrar, 5 leer el título (20 bytes), 6 guardar el
; nombre del usuario (16 bytes), 7 leerlo. La partida es FCB (el NGH) y el byte BIOS_CARD_SUB; los datos van de y a
; BIOS_CARD_START (BIOS_CARD_SIZE bytes). La respuesta en BIOS_CARD_ANSWER: 0 bien, $80 no hay tarjeta, $81 sin
; formato, $82 no está, $83 la FAT está mal, $84 no hay lugar, $85 protegida.
;
; El formato (bloques de 64 bytes; el tamaño en la cabecera es el que ocupa en el bus, el doble):
;   bloque 0     cabecera: $0A tamaño (.w), $0D/$0E suma de la FAT 1/2, $0F nombre puesto (0: no), $10 nombre,
;                $20 la marca "NEO-GEO",$80 en los bytes pares, $30 región
;   bloque 1...  directorio: una entrada de 4 bytes por bloque (sub, $FF libre; NGH; primer bloque)
;   luego        FAT 1 y FAT 2 (un byte por bloque: 0 libre, 1 el último, 2 reservado, si no el siguiente)
; La suma de cada FAT es la de sus bytes. Si una copia no da su suma se rehace con la otra; si ninguna, $83.
; Registros: a0 la tarjeta, a2/a3 las dos FAT (en el bus) durante todo el comando.
card:
        movem.l d0-d7/a0-a6,-(sp)
        moveq   #0,d6
        move.b  BIOS_CARD_COMMAND,d6
        cmpi.b  #7,d6
        bhi.s   .out                    ; no es un comando: no se toca nada
        move.b  #0,REG_CRDBANK
        move.b  d0,REG_CRDUNLOCK1
        move.b  d0,REG_CRDUNLOCK2
        lea     CARD_BASE,a0
        bsr.s   card_do
        move.b  d0,BIOS_CARD_ANSWER
        move.b  d0,REG_CRDLOCK1
        move.b  d0,REG_CRDLOCK2
.out:   movem.l (sp)+,d0-d7/a0-a6
        rts

; El comando d6; la respuesta en d0. El orden de los errores es el de la original
card_do:
        move.b  REG_STATUS_B,d0
        andi.b  #$30,d0
        bne.s   .none
        move.w  #%11001,d1              ; formatear, guardar y borrar miran primero la protección
        btst    d6,d1
        beq.s   .fmt
        btst    #6,REG_STATUS_B
        bne.s   .prot
        tst.b   d6
        beq.w   card_format
.fmt:   cmpi.b  #1,d6
        bne.s   .fmt2
        move.w  #0,BIOS_CARD_SUB        ; buscar: SUB en 0 aunque no tenga formato (como la original)
.fmt2:  bsr.w   card_formatted
        bne.s   .unf
        cmpi.b  #6,d6
        blo.s   .lay
        bne.w   card_user_load          ; 7
        btst    #6,REG_STATUS_B         ; 6: el nombre
        bne.s   .prot
        bra.w   card_user_save
.lay:   bsr.w   card_layout
        bne.s   .bad
        cmpi.b  #1,d6
        beq.w   card_search             ; buscar no mira la FAT
        bsr.w   card_fat_check
        bne.s   .bad
        cmpi.b  #3,d6
        blo.w   card_load
        beq.w   card_save
        cmpi.b  #4,d6
        beq.w   card_delete
        bra.w   card_title
.none:  move.b  #$80,d0
        rts
.unf:   move.b  #$81,d0
        rts
.bad:   move.b  #$83,d0
        rts
.prot:  move.b  #$85,d0
        rts

; Z si la tarjeta tiene la marca
card_formatted:
        lea     $40(a0),a1
        lea     card_magic(pc),a2
        moveq   #7,d1
.l:     move.b  (a1),d0
        cmp.b   (a2)+,d0
        bne.s   .x
        addq.l  #4,a1
        dbf     d1,.l
        cmp.b   d0,d0
.x:     rts

card_magic:
        dc.b    "NEO-GEO",$80

; Las zonas según el tamaño de la cabecera (2, 4, 8 o 16 KB): NB_CARD_*, a2 y a3. Z si el tamaño es uno de esos
card_layout:
        move.b  $14(a0),d0
        lsl.w   #8,d0
        move.b  $16(a0),d0
        moveq   #0,d1
        move.w  d0,d1
        cmpi.w  #$1000,d1
        beq.s   .ok
        cmpi.w  #$2000,d1
        beq.s   .ok
        cmpi.w  #$4000,d1
        beq.s   .ok
        cmpi.w  #$8000,d1
        bne.s   .x
.ok:    lsr.w   #7,d1                   ; bloques
        move.w  d1,NB_CARD_N
        move.w  d1,d2
        lsr.w   #4,d2
        addq.w  #1,d2                   ; la FAT 1: tras la cabecera y el directorio (N × 4 bytes)
        move.w  d1,d3
        addi.w  #63,d3
        lsr.w   #6,d3                   ; bloques de cada FAT
        move.w  d3,NB_CARD_FATB
        bsr.w   card_block
        movea.l a1,a2
        add.w   d3,d2
        bsr.w   card_block
        movea.l a1,a3
        add.w   d3,d2
        move.w  d2,NB_CARD_DATA
        cmp.b   d0,d0
.x:     rts

; a1 = el bloque d2 (en el bus). Usa d5
card_block:
        moveq   #0,d5
        move.w  d2,d5
        lsl.l   #7,d5
        lea     0(a0,d5.l),a1
        rts

; d0.b = FAT 1 del bloque d2. Usa d5
fat_get:
        move.w  d2,d5
        add.w   d5,d5
        move.b  0(a2,d5.w),d0
        rts

; FAT 1 del bloque d2 = d0.b (la 2 se copia al final, con fat_update). Usa d5
fat_set:
        move.w  d2,d5
        add.w   d5,d5
        move.b  d0,0(a2,d5.w)
        rts

; d0.b = la suma de la FAT que empieza en a1. Usa d1
fat_sum:
        move.w  NB_CARD_FATB,d1
        lsl.w   #6,d1
        subq.w  #1,d1
        moveq   #0,d0
.l:     add.b   (a1),d0
        addq.l  #2,a1
        dbf     d1,.l
        rts

; Copia la FAT de a4 a la de a5 (solo los bytes distintos). Usa d1, d4
fat_copy:
        move.w  NB_CARD_FATB,d1
        lsl.w   #6,d1
        subq.w  #1,d1
.l:     move.b  (a4),d4
        cmp.b   (a5),d4
        beq.s   .n
        move.b  d4,(a5)
.n:     addq.l  #2,a4
        addq.l  #2,a5
        dbf     d1,.l
        rts

; Tras cambiar la FAT 1: su suma, y la FAT 2 igual a ella
fat_update:
        movea.l a2,a1
        bsr.s   fat_sum
        move.b  d0,$1A(a0)
        movea.l a2,a4
        movea.l a3,a5
        bsr.s   fat_copy
        move.b  d0,$1C(a0)
        rts

; Z si hay una FAT buena (y entonces las dos quedan iguales)
card_fat_check:
        movea.l a2,a1
        bsr.s   fat_sum
        cmp.b   $1A(a0),d0
        bne.s   .bad1
        movea.l a2,a4
        movea.l a3,a5
        bsr.s   fat_copy
        cmp.b   $1C(a0),d0
        beq.s   .ok
        move.b  d0,$1C(a0)
.ok:    cmp.b   d0,d0
        rts
.bad1:  movea.l a3,a1
        bsr.s   fat_sum
        cmp.b   $1C(a0),d0
        bne.s   .x                      ; las dos mal
        movea.l a3,a4
        movea.l a2,a5
        bsr.s   fat_copy
        move.b  d0,$1A(a0)
        cmp.b   d0,d0
.x:     rts

; Libera la cadena que empieza en el bloque d2 (en la FAT 1). Usa d0, d2, d4, d5, d7
fat_free_chain:
        move.w  NB_CARD_N,d7
.l:     cmp.w   NB_CARD_DATA,d2
        blo.s   .x
        cmp.w   NB_CARD_N,d2
        bhs.s   .x
        bsr.w   fat_get
        move.b  d0,d4
        moveq   #0,d0
        bsr.w   fat_set
        moveq   #0,d2
        move.b  d4,d2
        dbf     d7,.l
.x:     rts

; a1 = la entrada de la partida d3.w (NGH), d4.b (sub); Z si está. Usa d0, d7
dir_find:
        lea     $80(a0),a1
        move.w  NB_CARD_N,d7
        subq.w  #1,d7
.l:     cmp.b   (a1),d4
        bne.s   .n
        move.b  2(a1),d0
        lsl.w   #8,d0
        move.b  4(a1),d0
        cmp.w   d0,d3
        beq.s   .f
.n:     addq.l  #8,a1
        dbf     d7,.l
        moveq   #1,d0
.f:     rts

; a1 = la primera entrada libre; Z si hay. Usa d0, d7
dir_free:
        lea     $80(a0),a1
        move.w  NB_CARD_N,d7
        subq.w  #1,d7
.l:     cmpi.b  #$FF,(a1)
        beq.s   .f
        addq.l  #8,a1
        dbf     d7,.l
        moveq   #1,d0
.f:     rts

; 1: las partidas del juego FCB (sub 0-15) como bits en BIOS_CARD_SUB
card_search:
        move.w  BIOS_CARD_FCB,d3
        moveq   #0,d2
        lea     $80(a0),a1
        move.w  NB_CARD_N,d7
        subq.w  #1,d7
.l:     moveq   #0,d1
        move.b  (a1),d1
        cmpi.b  #16,d1
        bhs.s   .n
        move.b  2(a1),d0
        lsl.w   #8,d0
        move.b  4(a1),d0
        cmp.w   d0,d3
        bne.s   .n
        bset    d1,d2
.n:     addq.l  #8,a1
        dbf     d7,.l
        move.w  d2,BIOS_CARD_SUB
        moveq   #0,d0
        rts

; 2: carga SIZE bytes; 5: los 20 del título (el principio de los datos)
card_load:
        move.w  BIOS_CARD_SIZE,d6
        bra.s   card_read
card_title:
        moveq   #20,d6
card_read:
        move.w  BIOS_CARD_FCB,d3
        move.b  BIOS_CARD_SUB,d4
        bsr.w   dir_find
        bne.s   .none
        moveq   #0,d2
        move.b  6(a1),d2
        movea.l BIOS_CARD_START,a4
        move.w  NB_CARD_N,d7            ; tope, por si la cadena da vueltas
.blk:   tst.w   d6
        beq.s   .ok
        cmp.w   NB_CARD_DATA,d2
        blo.s   .ok                     ; se acabó la cadena
        cmp.w   NB_CARD_N,d2
        bhs.s   .ok
        bsr.w   card_block
        moveq   #63,d1
.b:     move.b  (a1),(a4)+
        addq.l  #2,a1
        subq.w  #1,d6
        dbeq    d1,.b
        bsr.w   fat_get
        moveq   #0,d2
        move.b  d0,d2
        dbf     d7,.blk
.ok:    moveq   #0,d0
        rts
.none:  move.b  #$82,d0
        rts

; 3: guarda SIZE bytes. La cadena nueva se escribe antes de soltar la vieja (si la partida ya estaba), así que
; tiene que haber lugar para ella entera; la entrada nueva vale desde que se escribe su sub, lo último
card_save:
        move.w  BIOS_CARD_FCB,d3
        move.b  BIOS_CARD_SUB,d4
        moveq   #0,d0
        move.w  BIOS_CARD_SIZE,d0
        addi.l  #63,d0
        lsr.l   #6,d0                   ; bloques que hacen falta (al menos uno)
        bne.s   .n1
        moveq   #1,d0
.n1:    move.w  d0,d6
        moveq   #0,d1                   ; bloques libres
        move.w  NB_CARD_DATA,d2
.cnt:   cmp.w   NB_CARD_N,d2
        bhs.s   .cnted
        bsr.w   fat_get
        tst.b   d0
        bne.s   .used
        addq.w  #1,d1
.used:  addq.w  #1,d2
        bra.s   .cnt
.cnted: cmp.w   d6,d1
        blo.w   .full
        moveq   #0,d1                   ; d1: el primer bloque de la partida vieja (0: no había)
        bsr.w   dir_find
        bne.s   .new
        move.b  6(a1),d1
        bra.s   .alloc
.new:   bsr.w   dir_free
        bne.w   .full
.alloc: movea.l a1,a6                   ; la entrada
        movea.l BIOS_CARD_START,a4
        move.w  BIOS_CARD_SIZE,d7       ; bytes que faltan
        moveq   #0,d3                   ; el primer bloque nuevo
        moveq   #0,d4                   ; el anterior (0: ninguno)
        move.w  NB_CARD_DATA,d2
.find:  bsr.w   fat_get
        tst.b   d0
        beq.s   .got
        addq.w  #1,d2
        bra.s   .find
.got:   moveq   #1,d0                   ; por ahora el último
        bsr.w   fat_set
        tst.w   d4
        bne.s   .link
        move.w  d2,d3
        bra.s   .wr
.link:  move.w  d2,d0                   ; el anterior apunta a este
        move.w  d4,d2
        bsr.w   fat_set
        move.w  d0,d2
.wr:    move.w  d2,d4
        bsr.w   card_block
        moveq   #63,d5
.b:     tst.w   d7
        beq.s   .bd
        move.b  (a4)+,(a1)
        addq.l  #2,a1
        subq.w  #1,d7
        dbf     d5,.b
.bd:    subq.w  #1,d6
        bne.s   .find
        move.w  d1,-(sp)
        bsr.w   fat_update              ; la cadena nueva queda en las dos FAT (usa d1)
        move.w  (sp)+,d1
        move.b  d3,6(a6)
        tst.w   d1
        bne.s   .old
        move.w  BIOS_CARD_FCB,d0
        move.b  d0,4(a6)
        lsr.w   #8,d0
        move.b  d0,2(a6)
        move.b  BIOS_CARD_SUB,(a6)
        bra.s   .done
.old:   move.w  d1,d2
        bsr.w   fat_free_chain
        bsr.w   fat_update
.done:  moveq   #0,d0
        rts
.full:  move.b  #$84,d0
        rts

; 4: borra la partida
card_delete:
        move.w  BIOS_CARD_FCB,d3
        move.b  BIOS_CARD_SUB,d4
        bsr.w   dir_find
        bne.s   .none
        move.b  #$FF,(a1)
        moveq   #0,d2
        move.b  6(a1),d2
        bsr.w   fat_free_chain
        bsr.w   fat_update
        moveq   #0,d0
        rts
.none:  move.b  #$82,d0
        rts

; 6 y 7: el nombre del usuario (16 bytes en START)
card_user_save:
        movea.l BIOS_CARD_START,a4
        lea     $20(a0),a1
        moveq   #15,d1
.l:     move.b  (a4)+,(a1)
        addq.l  #2,a1
        dbf     d1,.l
        move.b  #$FF,$1E(a0)
        moveq   #0,d0
        rts
card_user_load:
        tst.b   $1E(a0)
        beq.s   .none
        movea.l BIOS_CARD_START,a4
        lea     $20(a0),a1
        moveq   #15,d1
.l:     move.b  (a1),(a4)+
        addq.l  #2,a1
        dbf     d1,.l
        moveq   #0,d0
        rts
.none:  move.b  #$82,d0
        rts

; 0: formatea. El tamaño se mide por dónde se repite la tarjeta (2, 4 u 8 KB; si no, 16 KB). La cabecera se
; borra primero y la marca se escribe al final: a medio formatear, la tarjeta queda sin formato
card_format:
        move.b  #0,(a0)
        move.w  #$800,d1
.sz:    moveq   #0,d5
        move.w  d1,d5
        add.l   d5,d5
        move.b  #$5A,0(a0,d5.l)
        cmpi.b  #$5A,(a0)
        beq.s   .got
        add.w   d1,d1
        cmpi.w  #$4000,d1
        blo.s   .sz
.got:   add.w   d1,d1                   ; en la cabecera, lo que ocupa en el bus
        movea.l a0,a1
        moveq   #63,d0
.z:     move.b  #0,(a1)
        addq.l  #2,a1
        dbf     d0,.z
        move.w  d1,d0
        move.b  d0,$16(a0)
        lsr.w   #8,d0
        move.b  d0,$14(a0)
        lea     $20(a0),a1              ; el nombre: 16 espacios
        moveq   #15,d0
.sp:    move.b  #$20,(a1)
        addq.l  #2,a1
        dbf     d0,.sp
        move.b  BIOS_COUNTRY_CODE,$60(a0)
        bsr.w   card_layout
        lea     $80(a0),a1              ; el directorio, libre
        move.w  NB_CARD_N,d0
        subq.w  #1,d0
.d:     move.b  #$FF,(a1)
        move.b  #0,2(a1)
        move.b  #0,4(a1)
        move.b  #0,6(a1)
        addq.l  #8,a1
        dbf     d0,.d
        movea.l a2,a1                   ; la FAT: reservado hasta los datos, libre después
        move.w  NB_CARD_FATB,d0
        lsl.w   #6,d0
        subq.w  #1,d0
        moveq   #0,d2
.f:     moveq   #0,d4
        cmp.w   NB_CARD_DATA,d2
        bhs.s   .fz
        moveq   #2,d4
.fz:    move.b  d4,(a1)
        addq.l  #2,a1
        addq.w  #1,d2
        dbf     d0,.f
        bsr.w   fat_update
        lea     $40(a0),a1              ; la marca
        lea     card_magic(pc),a4
        moveq   #7,d0
.m:     move.b  (a4)+,(a1)
        addq.l  #4,a1
        dbf     d0,.m
        moveq   #0,d0
        rts

; CARD_ERROR: en el AES solo atiende «protegida» ($85): lo dice en la pantalla y espera un botón. Tapa la capa
; fija (filas 2-29) y la deja como estaba al volver; esconde los sprites (los vuelve a poner el juego). El texto
; va con la letra de 8 × 16 de la ROM S del juego (mosaicos $100 + ASCII arriba y $200 + ASCII abajo), la misma
; que usa la original: en el AES la capa fija sale siempre del cartucho
card_error:
        cmpi.b  #$85,BIOS_CARD_ANSWER
        bne.w   .x
        movem.l d0-d7/a0-a6,-(sp)
        move    sr,-(sp)
        move    #$2700,sr
        lea     NB_FIX_SAVE,a1          ; guarda la capa fija
        move.w  #$7002,d2
        moveq   #39,d0
.sc:    move.w  d2,d3
        moveq   #27,d1
.sr:    move.w  d3,REG_VRAMADDR
        addq.w  #1,d3
        move.w  REG_VRAMRW,(a1)+
        dbf     d1,.sr
        addi.w  #32,d2
        dbf     d0,.sc
        move.l  PALETTES+2,NB_PAL_SAVE
        move.l  #$7FFF8000,PALETTES+2   ; letra blanca, sombra negra
        move.w  #1,REG_VRAMMOD
        move.w  #$8200,REG_VRAMADDR     ; sin sprites
        move.w  #380,d1
.hs:    move.w  #0,REG_VRAMRW
        dbf     d1,.hs
        move.w  #$7002,d2               ; la capa fija en blanco
        moveq   #39,d0
.cc:    move.w  d2,REG_VRAMADDR
        moveq   #27,d1
.cr:    move.w  #$0020,REG_VRAMRW
        dbf     d1,.cr
        addi.w  #32,d2
        dbf     d0,.cc
        lea     msg_protect1(pc),a1
        move.w  #$7000+5*32+12,d2       ; columna 5, filas 12-13
        bsr.w   big_text
        lea     msg_protect2(pc),a1
        move.w  #$7000+12*32+16,d2      ; columna 12, filas 16-17
        bsr.w   big_text
.rel:   move.b  d0,REG_DIPSW            ; que suelten lo que tenían apretado
        bsr.w   any_button
        bne.s   .rel
.prs:   move.b  d0,REG_DIPSW
        bsr.w   any_button
        beq.s   .prs
        lea     NB_FIX_SAVE,a1          ; la capa fija como estaba
        move.w  #$7002,d2
        moveq   #39,d0
.rc:    move.w  d2,REG_VRAMADDR
        moveq   #27,d1
.rr:    move.w  (a1)+,REG_VRAMRW
        dbf     d1,.rr
        addi.w  #32,d2
        dbf     d0,.rc
        move.l  NB_PAL_SAVE,PALETTES+2
        move    (sp)+,sr
        movem.l (sp)+,d0-d7/a0-a6
.x:     rts

msg_protect1:
        dc.b    "MEMORY CARD IS WRITE PROTECTED",0
msg_protect2:
        dc.b    "PRESS A BUTTON",0
        even

; Texto de 8 × 16 desde a1 (termina en 0) en la dirección de la capa fija d2 (mosaico de arriba). Usa d0, d2
big_text:
        moveq   #0,d0
        move.b  (a1)+,d0
        beq.s   .x
        move.w  d2,REG_VRAMADDR
        addi.w  #$0100,d0
        move.w  d0,REG_VRAMRW
        addi.w  #$0100,d0
        move.w  d0,REG_VRAMRW
        addi.w  #32,d2
        bra.s   big_text
.x:     rts

; Z si no hay ningún botón apretado (A-D de los dos mandos, los dos Start). Usa d0
any_button:
        move.b  REG_P1CNT,d0
        and.b   REG_P2CNT,d0
        not.b   d0
        andi.b  #$F0,d0
        bne.s   .x
        move.b  REG_STATUS_B,d0
        not.b   d0
        andi.b  #$05,d0
.x:     rts

; ==========================================================================================================
; FIX_CLEAR: la capa fija con el mosaico $FF y las columnas de los bordes con $20 (paleta 0). Usa d0, d1, a0
fix_clear:
        move.w  #$7000,REG_VRAMADDR
        move.w  #1,REG_VRAMMOD
        move.w  #$0020,d0
        moveq   #31,d1
.l:     move.w  d0,REG_VRAMRW
        dbf     d1,.l
        move.w  #$00FF,d0
        move.w  #38*32-1,d1
.m:     move.w  d0,REG_VRAMRW
        dbf     d1,.m
        move.w  #$0020,d0
        moveq   #31,d1
.r:     move.w  d0,REG_VRAMRW
        dbf     d1,.r
        rts

; LSP_1st: todos los sprites sin alto, sin reducción, en X 380; el mapa del sprite 0 con el mosaico $FF
lsp_1st:
        move.w  #1,REG_VRAMMOD
        move.w  #$8000,REG_VRAMADDR
        move.w  #$0FFF,d0
        move.w  #$1FF,d1
.s2:    move.w  d0,REG_VRAMRW
        dbf     d1,.s2
        move.w  #$8200,REG_VRAMADDR
        moveq   #0,d0
        move.w  #$1FF,d1
.s3:    move.w  d0,REG_VRAMRW
        dbf     d1,.s3
        move.w  #$8400,REG_VRAMADDR
        move.w  #$BE00,d0
        move.w  #$1FF,d1
.s4:    move.w  d0,REG_VRAMRW
        dbf     d1,.s4
        move.w  #$0000,REG_VRAMADDR
        moveq   #31,d1
.s1:    move.w  #$00FF,REG_VRAMRW
        move.w  #$0000,REG_VRAMRW
        dbf     d1,.s1
        rts

; ==========================================================================================================
; MESS_OUT: ejecuta el búfer de BIOS_MESS_BUFFER a BIOS_MESS_POINT: cada entrada es un puntero a una lista de
; comandos, o 0 y los comandos ahí mismo
mess_out:
        tst.b   BIOS_MESS_BUSY
        bne.s   .skip
        movem.l d0-d7/a0-a6,-(sp)
        lea     BIOS_MESS_BUFFER,a3
        movea.l BIOS_MESS_POINT,a4
.next:  cmpa.l  a4,a3
        bcc.s   .end
        move.l  (a3)+,d0
        bne.s   .ptr
        movea.l a3,a0                   ; comandos en el búfer
        bsr.w   mess_list
        movea.l a0,a3
        bra.s   .next
.ptr:   movea.l d0,a0
        bsr.w   mess_list
        bra.s   .next
.end:   move.l  #BIOS_MESS_BUFFER,BIOS_MESS_POINT
        movem.l (sp)+,d0-d7/a0-a6
.skip:  rts

; Una lista de comandos desde a0; vuelve con a0 después del comando 0. Las sublistas (A/B) usan la pila
mess_list:
.cmd:   move.w  (a0)+,d0
        move.w  d0,d1
        andi.w  #$FF,d1
        cmpi.w  #$0D,d1
        bhi.s   .cmd                    ; desconocido: se salta
        add.w   d1,d1
        move.w  .jt(pc,d1.w),d1
        jmp     .jt(pc,d1.w)
.jt:    dc.w    .c0-.jt,.c1-.jt,.c2-.jt,.c3-.jt,.c4-.jt,.c5-.jt,.c6-.jt,.c7-.jt
        dc.w    .c8-.jt,.c9-.jt,.ca-.jt,.cb-.jt,.cc-.jt,.cd-.jt
.c0:    rts                             ; fin
.c1:    move.w  d0,NB_FMT               ; formato
        move.w  (a0)+,NB_FMT_ARG
        bra.s   .cmd
.c2:    lsr.w   #8,d0                   ; incremento
        move.w  d0,NB_INC
        move.w  d0,REG_VRAMMOD
        bra.s   .cmd
.c3:    move.w  (a0)+,d0                ; dirección de la VRAM
        move.w  d0,NB_VRAM
        move.w  d0,REG_VRAMADDR
        bra.s   .cmd
.c4:    movea.l (a0)+,a1                ; datos en a1
        bsr.w   mess_data
        move.l  a1,NB_DATA
        bra.s   .cmd
.c5:    move.w  (a0)+,d0                ; sumar a la dirección
        add.w   d0,NB_VRAM
        move.w  NB_VRAM,REG_VRAMADDR
        bra.w   .cmd
.c6:    movea.l NB_DATA,a1              ; seguir con los datos de antes
        bsr.w   mess_data
        move.l  a1,NB_DATA
        bra.w   .cmd
.c7:    movea.l a0,a1                   ; datos ahí mismo
        bsr.w   mess_data
        move.l  a1,d0
        addq.l  #1,d0
        andi.w  #$FFFE,d0
        movea.l d0,a0
        bra.w   .cmd
.c8:    moveq   #0,d2                   ; texto de 8 × 16
        bra.s   .text
.c9:    moveq   #1,d2                   ; texto japonés
.text:  lsr.w   #8,d0
        move.w  d0,d3                   ; fuente
        move.w  #$20,NB_INC
        move.w  #1,REG_VRAMMOD
.ch:    moveq   #0,d0
        move.b  (a0)+,d0
        cmpi.b  #$FF,d0
        beq.s   .tend
        move.w  NB_VRAM,REG_VRAMADDR
        move.w  d3,d4                   ; mitad de arriba: fuente, carácter
        lsl.w   #8,d4
        move.w  d4,d5
        addi.w  #$100,d5                ; mitad de abajo: la página siguiente
        or.w    d0,d4
        or.w    d0,d5
        tst.w   d2
        beq.s   .put
        ; Japonés: $00-$1F y $60-$7F son kana de la página 0
        cmpi.b  #$20,d0
        bcs.s   .kana0
        cmpi.b  #$60,d0
        bcs.s   .put
        cmpi.b  #$7F,d0
        bhi.s   .put
        move.w  d0,d4
        addi.w  #$40,d4                 ; $60-$7F -> $A0-$BF arriba, $E0-$FF abajo
        bra.s   .kana
.kana0: move.w  d0,d4
        addi.w  #$80,d4                 ; $00-$1F -> $80-$9F arriba, $C0-$DF abajo
.kana:  move.w  d4,d5
        addi.w  #$40,d5
.put:   move.w  d4,REG_VRAMRW
        move.w  d5,REG_VRAMRW
        addi.w  #$20,NB_VRAM
        bra.s   .ch
.tend:  move.w  #$20,REG_VRAMMOD
        move.l  a0,d0                   ; alinear después del $FF
        addq.l  #1,d0
        andi.w  #$FFFE,d0
        movea.l d0,a0
        bra.w   .cmd
.ca:    move.l  (a0)+,d0                ; sublista
        move.l  a0,-(sp)
        movea.l d0,a0
        bsr.w   mess_list
        movea.l (sp)+,a0
        bra.w   .cmd
.cb:    rts                             ; fin de la sublista
.cc:    lsr.w   #8,d0                   ; repetir una palabra
        move.w  (a0)+,d1
        subq.w  #1,d0
        bcs.w   .cmd
.ccl:   move.w  d1,REG_VRAMRW
        bsr.w   vram_step
        dbf     d0,.ccl
        bra.w   .cmd
.cd:    lsr.w   #8,d0                   ; repetir y sumar 1 al byte bajo
        move.w  (a0)+,d1
        subq.w  #1,d0
        bcs.w   .cmd
.cdl:   move.w  d1,REG_VRAMRW
        bsr.w   vram_step
        move.b  d1,d2
        addq.b  #1,d2
        move.b  d2,d1
        dbf     d0,.cdl
        bra.w   .cmd

; La dirección que lleva MESS_OUT avanza con el incremento (la VRAM avanza sola)
vram_step:
        move.w  NB_INC,d7
        add.w   d7,NB_VRAM
        rts

; Escribe en la VRAM los datos de a1 según el formato; vuelve con a1 después de ellos (y del código de fin)
mess_data:
        move.w  NB_FMT,d4
        move.w  NB_FMT_ARG,d5
        btst    #9,d4
        bne.s   .words
        ; Bytes: el byte alto fijo y el código de fin o el tamaño
        move.w  d5,d6
        andi.w  #$FF00,d6               ; byte alto
        andi.w  #$FF,d5
        btst    #8,d4
        bne.s   .bsize
.bend:  moveq   #0,d0
        move.b  (a1)+,d0
        cmp.b   d5,d0
        beq.s   .ret
        or.w    d6,d0
        move.w  d0,REG_VRAMRW
        bsr.s   vram_step
        bra.s   .bend
.bsize: subq.w  #1,d5
        bcs.s   .ret
.bs:    moveq   #0,d0
        move.b  (a1)+,d0
        or.w    d6,d0
        move.w  d0,REG_VRAMRW
        bsr.s   vram_step
        dbf     d5,.bs
.ret:   rts
.words: btst    #8,d4
        bne.s   .wsize
.wend:  move.w  (a1)+,d0
        cmp.w   d5,d0
        beq.s   .ret
        move.w  d0,REG_VRAMRW
        bsr.s   vram_step
        bra.s   .wend
.wsize: subq.w  #1,d5
        bcs.s   .ret
.ws:    move.w  (a1)+,REG_VRAMRW
        bsr.s   vram_step
        dbf     d5,.ws
        rts

; --- Tabla de azar: 256 bytes en $C04200 ---
; Algunos juegos sacan números al azar de aquí (KOF '98: lea $C04200,a0 / move.b 0(a0,d0.w),d0). Es una
; permutación propia de 0-255 (Fisher-Yates con un congruencial lineal, semilla "NEO!"), no la de SNK: los
; juegos sacan otros números que con la BIOS original, pero cada valor sale una vez por vuelta.
        org     $C04200
random_table:
        dc.b    $6C,$63,$53,$1E,$32,$78,$89,$2D,$D7,$84,$FF,$29,$BD,$A5,$C8,$66
        dc.b    $75,$B4,$09,$EC,$B5,$7F,$62,$E1,$D9,$87,$F6,$37,$8E,$E6,$FC,$10
        dc.b    $FB,$C3,$68,$05,$BC,$26,$FA,$06,$64,$82,$B0,$2B,$7A,$6E,$72,$0E
        dc.b    $35,$D0,$D8,$80,$41,$76,$47,$38,$54,$A9,$A7,$0A,$1F,$3C,$0F,$BF
        dc.b    $25,$E0,$07,$96,$8F,$5A,$19,$18,$24,$51,$2C,$04,$36,$22,$CB,$4C
        dc.b    $23,$6B,$7C,$D6,$E2,$39,$A2,$AA,$B8,$6D,$F3,$4A,$F5,$58,$CA,$2E
        dc.b    $DD,$B7,$D4,$46,$86,$E7,$A8,$DC,$91,$C9,$50,$61,$BA,$E8,$DF,$C7
        dc.b    $E9,$A6,$59,$EA,$02,$73,$30,$D2,$13,$33,$5C,$8D,$CF,$9C,$3B,$74
        dc.b    $0D,$EB,$42,$D5,$03,$79,$6A,$45,$9E,$B3,$DA,$1D,$C2,$4F,$F4,$2A
        dc.b    $1C,$FE,$3F,$83,$3D,$1A,$69,$81,$99,$56,$67,$E3,$92,$E5,$C5,$8B
        dc.b    $3A,$94,$BB,$AD,$16,$34,$77,$70,$65,$0B,$BE,$CD,$11,$9F,$AC,$98
        dc.b    $CE,$9B,$B2,$A3,$F2,$F1,$F8,$5B,$97,$12,$44,$90,$B9,$40,$60,$C4
        dc.b    $43,$D3,$C1,$8C,$52,$00,$7D,$4B,$DB,$71,$55,$AE,$9A,$5E,$C6,$F7
        dc.b    $5D,$F0,$4E,$14,$7B,$3E,$A4,$7E,$AB,$08,$49,$85,$B1,$CC,$31,$48
        dc.b    $B6,$2F,$01,$C0,$A1,$27,$F9,$4D,$E4,$88,$93,$6F,$A0,$20,$8A,$15
        dc.b    $17,$0C,$28,$21,$ED,$95,$57,$D1,$1B,$AF,$DE,$9D,$FD,$EE,$5F,$EF
