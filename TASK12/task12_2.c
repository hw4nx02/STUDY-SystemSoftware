#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/wait.h>

int main() {
    pid_t pid; // 프로세스 ID를 저장하는 변수
    int status; // 자식 프로세스 종료시 상태 값을 저장할 변수
    pid = fork(); // 자식 프로세스 생성 -> 부모 프로세스에서는 자식 pid가, 자식 프로세스에서는 0이 반환됨
    putenv("APPLE=RED"); // 현재 프로세스의 환경변수 APPLE을 RED로 설정
    if(pid > 0) { // 부모 프로세스가 실행하는 블록
        printf("[parent] PID : %d\n", getpid()); // 현재 프로세스의 PID
        printf("[parent] PPID: %d\n", getppid()); // 현재 프로세스의 부모 PID
        printf("[parent] GID : %d\n", getpgrp()); // 현재 프로세스의 프로세스 그룹 ID
        printf("[parent] SID : %d\n", getsid(0)); // 현재 프로세스의 세션 ID 반환
        waitpid(pid, &status, 0); // 자식 프로세스(pid)의 종료 기다리고, status에 종료 상태 저장
        printf("[parent] status is %d\n", status); // status에는 보통 자식의 종료 코드가 특정 형식으로 저장됨
        unsetenv("APPLE"); // 부모 프로세스의 환경변수 APPLE 제거. 자식은 이미 exit 했으므로 자식에게는 영향 없음
    }
    else if(pid == 0) { // 자식 프로세스가 실행되는 블록
        printf("[child] PID : %d\n", getpid()); // 자식 pid
        printf("[child] PPID: %d\n", getppid()); // 부모 pid
        printf("[child] GID : %d\n", getpgid(0)); // 자식 프로세스의 그룹 id
        printf("[child] SID : %d\n", getsid(0)); // 자식 프로세스의 세션 id. 부모와 동일
        sleep(1); // 1초 대기 -> 부모 프로세스가 waitpid로 기다리게 되도록
        printf("[child] APPLE=%s\n", getenv("APPLE")); // 환경 변수 APPLE의 값 조회 -> 출력 예상 RED
        exit(1);
    } else printf("fail to fork\n"); // fork가 실패된 경우, 에러 메세지 출력
    return 0;
}