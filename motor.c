#include <pigpio.h>
#include <stdio.h>
#include <unistd.h>

// 핀 정의 (BCM 번호 기준)
#define SERVO_PIN 18 

int main(void) {
    // 1. pigpio 라이브러리 초기화
    if (gpioInitialise() < 0) {
        fprintf(stderr, "pigpio 초기화 실패\n");
        return 1;
    }

    printf("pigpio 서보모터 제어 시작 (BCM 18번 핀)\n");

    // --- 서보모터 동작 테스트 ---
    // gpioServo(핀번호, 펄스폭);
    // 펄스폭 범위: 500 (0도) ~ 1500 (90도) ~ 2500 (180도)
    // 0을 입력하면 신호 출력을 중단합니다.

    printf("0도로 이동\n");
    gpioServo(SERVO_PIN, 600); 
    sleep(2);

    printf("90도로 이동\n");
    gpioServo(SERVO_PIN, 1500);
    sleep(2);

    printf("180도로 이동\n");
    gpioServo(SERVO_PIN, 2400);
    sleep(2);

    // 신호 중단 (모터 힘 풀기)
    gpioServo(SERVO_PIN, 0);

    // 2. 라이브러리 종료
    gpioTerminate();
    printf("종료되었습니다.\n");

    return 0;
}