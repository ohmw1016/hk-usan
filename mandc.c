#include <pigpio.h>
#include <stdio.h>
#include <unistd.h>

#define PCA9685_ADDR 0x40    // PCA9685 기본 I2C 주소

// PCA9685 레지스터 정의
#define MODE1        0x00
#define PRESCALE     0xFE
#define LED0_ON_L    0x06

// 1번 세트 핀 정의 (BCM 및 PCA9685 채널 기준)
#define TRIG_1       24      // 라즈베리 파이 GPIO 24
#define ECHO_1       23      // 라즈베리 파이 GPIO 23
#define SERVO_1      0       // PCA9685 서보 채널 0번 (모터 1번)

// 펄스 값 매핑 (50Hz 기준)
#define PULSE_MIN    123     // 0도 부근 대기 상태 (-110도 방향)
#define PULSE_MAX    492     // 110도 회전 상태 (+110도 방향)

// PCA9685 특정 채널의 PWM 설정 함수
void set_pwm(int handle, int channel, int on, int off) {
    int reg = LED0_ON_L + (channel * 4);
    i2cWriteByteData(handle, reg,     on & 0xFF);
    i2cWriteByteData(handle, reg + 1, on >> 8);
    i2cWriteByteData(handle, reg + 2, off & 0xFF);
    i2cWriteByteData(handle, reg + 3, off >> 8);
}

// PCA9685 초기화 (50Hz 설정)
void init_pca9685(int handle) {
    i2cWriteByteData(handle, MODE1, 0x00);
    int mode1 = i2cReadByteData(handle, MODE1);
    int sleep_mode = (mode1 & 0x7F) | 0x10;
    i2cWriteByteData(handle, MODE1, sleep_mode);
    i2cWriteByteData(handle, PRESCALE, 121); // 50Hz
    int wake_mode = sleep_mode & 0xEF;
    i2cWriteByteData(handle, MODE1, wake_mode);
    gpioDelay(5000);
    i2cWriteByteData(handle, MODE1, wake_mode | 0xA1);
}

// 1번 초음파 센서 거리 측정 함수
double get_distance_1() {
    gpioWrite(TRIG_1, 1);
    gpioDelay(10); // 10us
    gpioWrite(TRIG_1, 0);

    uint32_t startTick = gpioTick();
    uint32_t endTick = startTick;

    int timeout = 1000000; 
    while (gpioRead(ECHO_1) == 0 && --timeout > 0);
    startTick = gpioTick();

    timeout = 1000000;
    while (gpioRead(ECHO_1) == 1 && --timeout > 0);
    endTick = gpioTick();

    uint32_t diff = endTick - startTick;
    return (double)diff / 58.0;
}

int main(void) {
    if (gpioInitialise() < 0) {
        printf("pigpio 초기화 실패!\n");
        return 1;
    }

    // I2C 오픈
    int handle = i2cOpen(1, PCA9685_ADDR, 0);
    if (handle < 0) {
        printf("PCA9685 연결 실패!\n");
        gpioTerminate();
        return 1;
    }

    init_pca9685(handle);

    // 초음파 핀 모드 설정
    gpioSetMode(TRIG_1, PI_OUTPUT);
    gpioSetMode(ECHO_1, PI_INPUT);

    // 초기 상태: 모터 1번을 대기 위치(-110도 방향)로 정렬 후 전력 차단
    set_pwm(handle, SERVO_1, 0, PULSE_MIN);
    sleep(1);
    set_pwm(handle, SERVO_1, 0, 0);

    printf("=== PCA9685 + 초음파 1번 연동 시스템 가동 ===\n");

    while (1) {
        double dist = get_distance_1();

        if (dist > 1.0 && dist < 100.0) {
            printf("Slot 1 Distance: %.2f cm\r", dist);
            fflush(stdout);

            // 5cm 근처 감지 (4.5cm ~ 5.5cm)
            if (dist >= 4.5 && dist <= 5.5) {
                printf("\n[ALERT] 1번 슬롯 우산 감지! 잠금 작동.\n");
                
                // 모터 1번 110도 방향 구동
                gpioDelay(5000000);
                set_pwm(handle, SERVO_1, 0, PULSE_MAX); 
                sleep(2); // 회전 시간 확보
                
                printf("1번 잠금 완료. 5초 대기...\n");
                sleep(5);  
                
                // 모터 부하 및 떨림 방지를 위해 전류 차단
                set_pwm(handle, SERVO_1, 0, 0); 
                printf("대기 모드 복귀.\n");
            }
        }
        gpioDelay(100000); // 100ms 대기
    }

    i2cClose(handle);
    gpioTerminate();
    return 0;
}