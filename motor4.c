#include <pigpio.h>
#include <stdio.h>
#include <unistd.h>

#define PCA9685_ADDR 0x40    // PCA9685 기본 I2C 주소

// PCA9685 레지스터 정의
#define MODE1        0x00
#define PRESCALE     0xFE
#define LED0_ON_L    0x06

// 서보모터 연결 채널 (PCA9685의 0, 1, 2, 3번 핀)
#define SERVO_1      0
#define SERVO_2      1
#define SERVO_3      2
#define SERVO_4      3

// $50\text{Hz}$($20\text{ms}$) 주기에서 12비트($0 \sim 4095$) 해상도 기준 펄스 값 계산
// 600us  -> (600 / 20000) * 4096 = 약 123
// 2400us -> (2400 / 20000) * 4096 = 약 492
#define PULSE_MIN    123  // -110도 방향 대칭
#define PULSE_MAX    492  // +110도 방향 대칭

// PCA9685 특정 채널의 PWM(펄스 폭)을 설정하는 함수
void set_pwm(int handle, int channel, int on, int off) {
    int reg = LED0_ON_L + (channel * 4);
    i2cWriteByteData(handle, reg,     on & 0xFF);
    i2cWriteByteData(handle, reg + 1, on >> 8);
     i2cWriteByteData(handle, reg + 2, off & 0xFF);
    i2cWriteByteData(handle, reg + 3, off >> 8);
}

// PCA9685 초기화 및 50Hz 주파수 설정 함수
void init_pca9685(int handle) {
    i2cWriteByteData(handle, MODE1, 0x00); // 칩 깨우기 (Restart)
    
    // 주파수 설정을 위해 잠시 슬립 모드로 진입
    int mode1 = i2cReadByteData(handle, MODE1);
    int sleep_mode = (mode1 & 0x7F) | 0x10;
    i2cWriteByteData(handle, MODE1, sleep_mode);
    
    // 50Hz 주파수를 위한 Prescale 값 계산 (25000000 / (4096 * 50)) - 1 = 121
    i2cWriteByteData(handle, PRESCALE, 121);
    
    // 슬립 모드 해제
    int wake_mode = sleep_mode & 0xEF;
    i2cWriteByteData(handle, MODE1, wake_mode);
    gpioDelay(5000); // 안정화 대기 (5ms)
    i2cWriteByteData(handle, MODE1, wake_mode | 0xA1);
}

int main(void) {
    if (gpioInitialise() < 0) {
        printf("pigpio 초기화 실패!\n");
        return 1;
    }

    // I2C 버스 1번, 주소 0x40으로 연결 오픈
    int handle = i2cOpen(1, PCA9685_ADDR, 0);
    if (handle < 0) {
        printf("PCA9685 I2C 연결 실패!\n");
        gpioTerminate();
        return 1;
    }

    // PCA9685 초기화 (50Hz 설정)
    init_pca9685(handle);
    printf("=== PCA9685 기반 4채널 서보모터 제어 가동 ===\n");

    while (1) {
        printf("\n[STEP 1] 1,2번 -> 최소각(-110도) / 3,4번 -> 최대각(+110도)\n");
        set_pwm(handle, SERVO_1, 0, PULSE_MIN);
        set_pwm(handle, SERVO_2, 0, PULSE_MIN);
        set_pwm(handle, SERVO_3, 0, PULSE_MAX);
        set_pwm(handle, SERVO_4, 0, PULSE_MAX);
        sleep(3);

        printf("[STEP 2] 1,2번 -> 최대각(+110도) / 3,4번 -> 최소각(-110도)\n");
        set_pwm(handle, SERVO_1, 0, PULSE_MAX);
        set_pwm(handle, SERVO_2, 0, PULSE_MAX);
        set_pwm(handle, SERVO_3, 0, PULSE_MIN);
        set_pwm(handle, SERVO_4, 0, PULSE_MIN);
        sleep(3);

        printf("[OFF] 모터 전력 차단 및 떨림 방지\n");
        set_pwm(handle, SERVO_1, 0, 0);
        set_pwm(handle, SERVO_2, 0, 0);
        set_pwm(handle, SERVO_3, 0, 0);
        set_pwm(handle, SERVO_4, 0, 0);
        
        printf("5초 후 다시 시작합니다...\n");
        sleep(5);
    }

    i2cClose(handle);
    gpioTerminate();
    return 0;
}