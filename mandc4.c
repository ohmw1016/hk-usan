#include <pigpio.h>
#include <stdio.h>
#include <unistd.h>

#define PCA9685_ADDR 0x40    // PCA9685 기본 I2C 주소

// PCA9685 레지스터 정의
#define MODE1        0x00
#define PRESCALE     0xFE
#define LED0_ON_L    0x06

// 슬롯 총 개수
#define NUM_SLOTS    4

// 펄스 값 매핑 (50Hz 기준)
// 1, 2번은 최소각 대기 / 3, 4번은 최대각 대기 등 구조에 맞게 커스텀 가능하도록 초기값 분리
#define PULSE_MIN    200     // -110도 방향 대칭 (기본 대기 상태)
#define PULSE_MAX    492     // +110도 방향 대칭 (잠금 상태)

// 각 슬롯의 하드웨어 핀 및 상태 정보를 묶는 구조체 정의
typedef struct {
    int slot_num;      // 슬롯 번호 (1~4)
    int trig_pin;      // 라즈베리 파이 Trig BCM 핀
    int echo_pin;      // 라즈베리 파이 Echo BCM 핀
    int servo_ch;      // PCA9685 서보 채널 번호
    int is_locked;     // 현재 잠금 상태 플래그 (0: 대기, 1: 잠금)
    uint32_t lock_time;// 잠금 시작된 시점 저장 (논블로킹 타이머용)
} UmbrellaSlot;

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

// 특정 슬롯의 초음파 센서 거리 측정 함수
double get_distance(int trig, int echo) {
    gpioWrite(trig, 1);
    gpioDelay(10); // 10us
    gpioWrite(trig, 0);

    uint32_t startTick = gpioTick();
    uint32_t endTick = startTick;

    int timeout = 50000; // 다중 센서 스캔을 위해 타임아웃 단축 (약 50ms)
    while (gpioRead(echo) == 0 && --timeout > 0);
    startTick = gpioTick();

    timeout = 50000;
    while (gpioRead(echo) == 1 && --timeout > 0);
    endTick = gpioTick();

    if (timeout <= 0) return -1.0; // 측정 실패 예외처리

    uint32_t diff = endTick - startTick;
    return (double)diff / 58.0;
}

int main(void) {
    if (gpioInitialise() < 0) {
        printf("pigpio 초기화 실패!\n");
        return 1;
    }

    int handle = i2cOpen(1, PCA9685_ADDR, 0);
    if (handle < 0) {
        printf("PCA9685 연결 실패!\n");
        gpioTerminate();
        return 1;
    }

    init_pca9685(handle);

    // 4개 슬롯의 하드웨어 핀 맵 설정 (보고서에 작성한 핀맵 적용)
    UmbrellaSlot slots[NUM_SLOTS] = {
        {1, 24, 23, 0, 0, 0}, // 1번 슬롯: Trig=24, Echo=23, PCA 서보=0
        {2, 17, 27, 1, 0, 0}, // 2번 슬롯: Trig=17, Echo=27, PCA 서보=1
        {3, 22, 10, 2, 0, 0}, // 3번 슬롯: Trig=22, Echo=10, PCA 서보=2
        {4,  9, 11, 3, 0, 0}  // 4번 슬롯: Trig=9,  Echo=11, PCA 서보=3
    };

    // 모든 슬롯의 GPIO 핀 모드 초기화 및 모터 초기 정렬
    for (int i = 0; i < NUM_SLOTS; i++) {
        gpioSetMode(slots[i].trig_pin, PI_OUTPUT);
        gpioSetMode(slots[i].echo_pin, PI_INPUT);
        
        // 초기 대기 상태로 모터 위치 고정 후 전력 차단
        set_pwm(handle, slots[i].servo_ch, 0, PULSE_MIN);
        gpioDelay(500000); // 0.5초 대기
        set_pwm(handle, slots[i].servo_ch, 0, 0);
    }

    printf("=== PCA9685 기반 스마트 우산 보관함 4채널 멀티 슬롯 가동 ===\n");

    while (1) {
        for (int i = 0; i < NUM_SLOTS; i++) {
            // 이미 잠금 상태인 슬롯은 타임아웃(5초) 체크를 하여 복귀 루틴 수행
            if (slots[i].is_locked) {
                // 현재 시간(초)이 잠금 시작 시간 + 5초를 지났는지 체크
                if (time(NULL) - slots[i].lock_time >= 5) {
                    printf("\n[INFO] %d번 슬롯 5초 대기 종료 -> 대기 모드 복귀.\n", slots[i].slot_num);
                    set_pwm(handle, slots[i].servo_ch, 0, 0); // 모터 전력 차단 (떨림 방지)
                    slots[i].is_locked = 0; // 플래그 초기화
                }
                continue; // 이미 잠긴 슬롯은 거리 측정을 건너뛰고 다음 슬롯으로 고!
            }

            // 대기 상태인 슬롯만 초음파 측정 진행
            double dist = get_distance(slots[i].trig_pin, slots[i].echo_pin);

            if (dist > 1.0 && dist < 100.0) {
                printf("Slot %d: %.2f cm | ", slots[i].slot_num, dist);
                fflush(stdout);

                // 5cm 근처 감지 (4.5cm ~ 5.5cm)
                if (dist >= 4.5 && dist <= 5.5) {
                    printf("\n[ALERT] %d번 슬롯 우산 감지! 잠금 시퀀스 시작.\n", slots[i].slot_num);
                    
                    // 모터 구동 처리 전 아주 잠깐의 타이밍 안정화 버퍼 (50ms)
                    gpioDelay(50000); 
                    
                    // 해당 슬롯 모터 110도 방향 회전
                    set_pwm(handle, slots[i].servo_ch, 0, PULSE_MAX);
                    
                    // 상태 업데이트
                    slots[i].is_locked = 1;
                    slots[i].lock_time = time(NULL); // 현재 시점 초 단위 저장
                }
            }
            gpioDelay(30000); // 센서 간 혼선(잔향 크로스토크) 방지를 위한 슬롯 간 30ms 마진
        }
        printf("\r");
        gpioDelay(100000); // 전체 스캔 루프 사이 100ms 대기
    }

    i2cClose(handle);
    gpioTerminate();
    return 0;
}