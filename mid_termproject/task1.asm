COPY     START   0
MAIN     LDA     ZERO
         LDL     ZERO
         LDX     ZERO
         JSUB    CALB
         JSUB    BTOB2
         JSUB    B2TOB3
         J       EXIT
CALB     LDX     ZERO
         LDA     ZERO
         STA     BUFLEN
CLOOP    LDCH    BUFFER,X
         COMP    EOB
         JEQ     RETURN
         LDA     BUFLEN
         ADD     ONE
         STA     BUFLEN
         LDX     BUFLEN
         J       CLOOP
RETURN  RSUB
BTOB2    LDA     ZERO
         LDX     ZERO
LOOPB    LDCH    BUFFER,X
         STCH    BUFFERA,X
         TIX     BUFLEN
         JLT     LOOPB
         RSUB
B2TOB3   LDA     ZERO
         LDX     ZERO
LOOPB2   LDCH    BUFFERA,X
         STCH    BUFFERB,X
         TIX     BUFLEN
         JLT     LOOPB2
         RSUB
ZERO     WORD    0
ONE      WORD    1
BUFLEN   RESW    1
BUFFER   BYTE    C'printf("HELLO, WORLD!");'
EOB      WORD    0
BUFFERA  RESB    100
BUFFERB  RESB    100
EXIT     J      EXIT
         END     MAIN