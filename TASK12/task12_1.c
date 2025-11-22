#include <sys/types.h>  // pid_t 등의 시스템 자료형 정의
#include <stdio.h>      // printf, fprintf 사용
#include <errno.h>      // 전역 errno 변수 및 에러 코드 정의
#include <string.h>     // strerror() 사용 (에러 코드를 문자열로 변환)
#include <stdlib.h>     // exit() 사용
#include <unistd.h>     // fork(), getpid(), getppid(), sleep() 등 POSIX 함수

// 부모 프로세스 - 자식 1 프로세스 - 자식 1의 자식인 2의 프로세스 - ....
// 프로세스의 부모 자식 관계는 위와 같이 계층적으로 계속 이어지는 구조
int main(int argc, char **argv) {
    pid_t pid, old_ppid, new_ppid;
    // pid_t : 프로세스 ID를 표현하는 정수형 자료형
    // old_ppid : 자식이 처음에 관측한 부모 PID
    // new_ppid : 부모가 종료한 뒤 자식이 다시 읽은 PPID(대부분 init 또는 systemd PID인 1)

    pid_t child, parent;
    // child : fork() 반환값 저장용 (부모에서는 자식 PID, 자식에서는 0)
    // parent : 현재 프로세스의 PID 저장

    parent = getpid();
    // getpid() : 현재 실행 중인 프로세스의 PID 반환
    // -> parent에는 현재 실행 중인 프로세스(= 부모 프로세스)의 PID가 저장됨

    if((child = fork()) < 0) {
        // fork() : 부모 프로세스의 메모리를 복제하여 새로운 자식 프로세스 생성하고 자식 프로세스 ID 반환
        // -> 반환값: 부모 프로세스일 때 -> 자식의 PID(프로세스 아이디)
        //            자식 프로세스일 때 -> 0
        //            실패 -> -1
        // => fork를 통해 현재 프로세스 시점에서부터 이어서 실행되는 똑같은 프로세스, 즉 자식 프로세스를 생성.
        // 이를 통해 부모와 자식 프로세스 각각 자식 프로세스인지 여부에 따라 child 값을 초기화
    
        // fork는 부모와 자식을 어떻게 구분하는가?
        // -> 현재 fork 호출 시점 기준,
        // 부모 프로세스였다면 부모 프로세스로, 자식 프로세스였다면 자식 프로세스로 인식하고 그때의 반환값을 제시한다.

        fprintf(stderr, "%s: fork of child failed: %s\n", argv[0], strerror(errno));
        // errno 값을 문자열로 반환 -> 에러 메세지 출력: 자식 fork 실패했다는 에러 메세지
        exit(1);
        // fork 실패 시 프로그램 종료

        // 자식 프로세스의 생성 후 부모, 자식 중 어떤 것이 먼저 수행될지는 보장할 수 없음.
    } else if (child == 0) {
        // 자식 프로세스만 실행하는 영역
        
        old_ppid = getppid();
        // getppid() : 부모 프로세스의 PID를 반환
        // old_ppid 에는 부모 PID 저장됨
        sleep(2);
        // 2초 대기
        // 이 동안 부모는 아래 else 블록에서 sleep(1) 후 exit(0)로 종료됨
        // 부모 프로세스가 죽으면 자식의 PPID는 1(초기화 pid)로 변경됨

        new_ppid = getppid();
        // 부모 종료 후 ppid를 다시 확인
        // 기댓값: 1 or systemd로 설정된 id값(실행 결과, 내 PC에서는 420이었음) (부모가 죽었으니까)
    } else {
        // child > 0 부모 프로세스가 실행되는 영역
        // child 변수에는 생성된 자식 프로세스의 pid가 들어있다

        sleep(1);
        // 부모는 1초 대기
        // 자식은 2초 대기이므로 부모가 먼저 종료

        exit(0);
        // 부모 종료
    }

    // 자식 프로세스만 실행할 수 있는 부분
    printf("Original parent: %d\n", parent); // 처음 시작할 때, getpid()를 통해 구했으므로 현재 실행 중인 프로세스이자 앞으로 생성될 자식 프로세스의 부모의 pid값 반환
    printf("Child: %d\n", getpid()); // 현재 자식 프로세스의 pid 값
    printf("Child's old ppid: %d\n", old_ppid); // 부모 프로세스가 죽기 전에 받은 값이므로, 자식 프로세스를 생성한 부모 프로세스의 pid.
    printf("Child's new ppid: %d\n", new_ppid); // 부모 프로세스가 자식 프로세스보다 먼저 죽은 후에 받은 부모 프로세스의 값. 즉, 자식 프로세스가 부모를 잃었을 때 이러한 자식 프로세스를 가져가는 프로세스의 pid를 알 수 있다. 여러 번 시행을 해보면 이는 동일한 값을 유지함을 볼 수 있음.
    exit(0);
}
