TASK5    START   3
MAIN     LDS     #0
         LDA     #0
         LDX     #0
         LDT     #0
         LDB     #0
         LDL     #0
         STA     COUNT
         JSUB    COPY
         J       EXIT
         USE     CONSTANT
STRING   BYTE    C'YELLOW IS A CUTEST DOG'
STREND   EQU     *
STRLEN   EQU     STREND-STRING
         USE     VARIABLE
COUNT    RESW    1
         USE     BUFFER
BUFFER   RESB    2200
BUFEND   EQU     *
MAXLEN   EQU     BUFEND-BUFFER
         USE     
COPY     LDT     #MAXLEN
CLOOP    COMPR   S,T
         JEQ     RETURN
         LDCH    STRING,X
         STX     COUNT
         RMO     S,X
         STCH    BUFFER,X
         LDA     #1
         ADDR    A,S
         LDX     COUNT
         TIX     #STRLEN
         JLT     CLOOP
         LDX     #0
         J       CLOOP
RETURN   RSUB    
EXIT     J       EXIT
         END     MAIN