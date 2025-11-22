#include <sys/types.h>
#include <sys/wait.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

int main() {
    pid_t pid;
    int ptc[2];
    int ctp[2];
    char input[100];
    int status;
        
    // pipe 생성
    if (pipe(ptc) == -1) {
        perror("pipe: ptc error");
        exit(1);
    }
    if (pipe(ctp) == -1) {
        perror("pipe: ctp error");
        exit(1);
    }

    // 자식 프로세스 생성
    pid = fork();

    if (pid < 0) { // 자식 프로세스 생성되지 않음
        perror("fail to fork\n");
        exit(1);
    } else if (pid == 0) { // 자식 프로세스이면
        sleep(2);

        // parent -> child: 파이프로 입력값 읽기
        close(ptc[1]);
        read(ptc[0], input, sizeof(input));
        close(ptc[0]);

        // 공백 기준 input 구분 및 구구단 계산
        int result[9];
        int step = atoi(input);
        for (int j = 0; j < 9; j++) {
            result[j] = step * (j + 1);
        }

        // child -> parent
        close(ctp[0]);
        write(ctp[1], result, sizeof(result));
        close(ctp[1]);

        exit(0);
    } else { // 부모 프로세스이면
        // 구구단 단수 입력
        printf("구구단 단수를 입력하세요: ");
        fgets(input, sizeof(input), stdin);

        // parent -> child: 입력값을 파이프에 쓰기
        close(ptc[0]);
        write(ptc[1], input, strlen(input) + 1);
        close(ptc[1]);

        // 자식 프로세스 종료 대기
        wait(&status);

        // child -> parent: 자식 프로세스에서 파이프에 작성한 값 읽기
        int output[9];
        close(ctp[1]);
        read(ctp[0], output, sizeof(output));
        close(ctp[0]);

        printf("Output\n");
        for (int i = 0; i < 9; i++) {
            printf("%d * %d = %d\n", atoi(input), i+1, output[i]);
        }
    }
}