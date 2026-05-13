#include <pigpio.h>
#include <stdio.h>
#include <unistd.h>

// 핀 정의 (BCM 번호 기준)
#define TRIG_PIN 24
#define ECHO_PIN 23
#define SERVO_PIN 18

// 거리 측정 함수
double get_distance() {
    gpioWrite(TRIG_PIN, 1);
    gpioDelay(10); // 10us
    gpioWrite(TRIG_PIN, 0);

    uint32_t startTick = gpioTick();
    uint32_t endTick = startTick;

    // Echo 핀이 High가 될 때까지 대기
    int timeout = 1000000; 
    while (gpioRead(ECHO_PIN) == 0 && --timeout > 0);
    startTick = gpioTick();

    // Echo 핀이 Low가 될 때까지 대기
    timeout = 1000000;
    while (gpioRead(ECHO_PIN) == 1 && --timeout > 0);
    endTick = gpioTick();

    uint32_t diff = endTick - startTick;
    return (double)diff / 58.0; // cm 환산
}

int main(void) {
    if (gpioInitialise() < 0) {
        printf("pigpio 초기화 실패!\n");
        return 1;
    }

    gpioSetMode(TRIG_PIN, PI_OUTPUT);
    gpioSetMode(ECHO_PIN, PI_INPUT);
    gpioSetMode(SERVO_PIN, PI_OUTPUT);

    printf("=== pigpio 기반 5cm 잠금 시스템 가동 ===\n");

    while (1) {
        double dist = get_distance();

        if (dist > 1.0 && dist < 100.0) {
            printf("Distance: %.2f cm\r", dist);
            fflush(stdout);

            // 5cm 근처 감지 (4.5cm ~ 5.5cm)
            if (dist >= 4.5 && dist <= 5.5) {
                printf("\n[ALERT] 우산 감지! 잠금 시퀀스 시작.\n");
                
                // pigpio의 하드웨어 PWM 기능을 사용하여 90도(1500us)로 이동
                // 훨씬 안정적으로 모터를 잡아줍니다.
                gpioServo(SERVO_PIN, 1500); 
                
                sleep(2); // 모터가 회전할 충분한 시간
                printf("잠금 완료. 5초 대기...\n");
                
                sleep(5); 
                
                // 모터 전력 소모 및 떨림 방지를 위해 신호 차단
                gpioServo(SERVO_PIN, 0); 
            }
        }
        gpioDelay(100000); // 100ms 대기
    }

    gpioTerminate();
    return 0;
}