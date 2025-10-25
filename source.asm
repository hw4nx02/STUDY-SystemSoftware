COPY    START   1000
FIRST   STL     RETADR
CLOOP   JSUB    RDREC
        LDA     LENGTH
        COMP    ZERO
        JEQ     ENDFIL
        JSUB    WRREC
        J       CLOOP
ENDFIL  LDA     EOF
        STA     BUFFER
        RSUB
RETADR  RESW    1
LENGTH  WORD    4096
ZERO    WORD    0
EOF     BYTE    C'EOF'
BUFFER  RESB    4096
        END     FIRST
