FIRST    START   3
MAIN     LDA     #0
         LDX     #0
         LDS     #0
         LDT     #0
         LDB     #0
         JSUB    ADDR
         JSUB    RTOR
         JSUB    LIT
         JSUB    EEQU
         JSUB    EORG
         +J      EXIT
ADDR     LDA     ONE
         LDA     #2
         STL     RETADR
         LDL     #THREE
         STL     THRPTR
         LDA     @THRPTR
         +LDB    #FOUR
         BASE    FOUR
         LDA     FOUR
         LDL     RETADR
         RSUB    
RTOR     LDS     #4
         LDT     #1
         ADDR    S,T
         RSUB    
LIT      LDA     =4276545
         LDS     =C'AAA'
         LDT     =X'414141'
         LTORG
         RSUB    
EEQU     LDA     #FIVE1
         LDA     #0
         LDA     FIVE1
         LDA     #0
         LDA     FIVE2
         RSUB    
EORG     LDA     =20
         STA     NUM1
         LDA     =25
         STA     NUM2
         LDX     #10
         LDA     =9
         STA     NUM1,X
         LDA     =25
         STA     NUM2,X
         RSUB    
ONE      WORD    1
THREE    WORD    3
THRPTR   RESW    1
RETADR   RESW    1
FIVE     WORD    5
FIVE1    EQU     5
FIVE2    EQU     FIVE
BUFFERA  RESB    4096
         ORG     BUFFERA
NUM1     RESW    1
NUM2     RESW    1
         ORG     BUFFERA+4096
FOUR     WORD    4
EXIT     J       EXIT
         END     MAIN