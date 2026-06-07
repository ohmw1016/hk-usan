#include <pigpio.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#define PCA9685_ADDR 0x40    // PCA9685 기본 I2C 주소

// PCA9685 레지스터 정의
#define MODE1        0x00
#define PRESCALE     0xFE
#define LED0_ON_L    0x06

// 슬롯 총 개수
#define NUM_SLOTS    4

// =================================================================
// 💡 [대칭 개방 매커니즘] 슬롯 그룹별 개별 맞춤형 각도 정의
// =================================================================
#define S13_LOCK_PULSE    200   // 1,3번 잠김 각도 (최소값)
#define S13_OPEN_PULSE    450   // 1,3번 열림 각도 

#define S24_LOCK_PULSE    450   // 2,4번 잠김 각도 (최대값)
#define S24_OPEN_PULSE    200   // 2,4번 열림 각도 
// =================================================================

typedef struct {
    int slot_num;        
    int trig_pin;        
    int echo_pin;        
    int servo_ch;        
    int is_locked;       
    uint32_t lock_time;  
} UmbrellaSlot;

void set_pwm(int handle, int channel, int on, int off) {
    int reg = LED0_ON_L + (channel * 4);
    i2cWriteByteData(handle, reg,     on & 0xFF);
    i2cWriteByteData(handle, reg + 1, on >> 8);
    i2cWriteByteData(handle, reg + 2, off & 0xFF);
    i2cWriteByteData(handle, reg + 3, off >> 8);
}

void init_pca9685_safely(int handle) {
    int mode1 = i2cReadByteData(handle, MODE1);
    if (mode1 < 0) mode1 = 0x00;

    int sleep_mode = (mode1 & 0x7F) | 0x10;
    i2cWriteByteData(handle, MODE1, sleep_mode); 
    
    i2cWriteByteData(handle, PRESCALE, 121); // 50Hz

    set_pwm(handle, 0, 0, S13_LOCK_PULSE); 
    set_pwm(handle, 1, 0, S24_LOCK_PULSE); 
    set_pwm(handle, 2, 0, S13_LOCK_PULSE); 
    set_pwm(handle, 3, 0, S24_LOCK_PULSE); 
    gpioDelay(1000); 

    int wake_mode = sleep_mode & 0xEF;
    i2cWriteByteData(handle, MODE1, wake_mode);
    gpioDelay(5000); 
    
    i2cWriteByteData(handle, MODE1, wake_mode | 0xA1); 
}

// ⭐ [개선] 절대 시간(us) 기준 논블로킹 타임아웃 초음파 측정 함수
double get_distance(int trig, int echo) {
    gpioWrite(trig, 0);
    gpioDelay(2);
    
    gpioWrite(trig, 1);
    gpioDelay(10); 
    gpioWrite(trig, 0);

    uint32_t start_timeout = gpioTick();
    
    // 1. Echo 핀이 HIGH(1)가 될 때까지 대기 (최대 3ms / 약 50cm 거리분)
    while (gpioRead(echo) == 0) {
        if ((gpioTick() - start_timeout) > 3000) {
            return -1.0; // 3ms 동안 반응 없으면 지체 없이 실패 리턴
        }
    }
    uint32_t startTick = gpioTick();

    // 2. Echo 핀이 다시 LOW(0)가 될 때까지 대기 (최대 3ms)
    start_timeout = gpioTick();
    while (gpioRead(echo) == 1) {
        if ((gpioTick() - start_timeout) > 3000) {
            return -1.0; // 신호가 끊기지 않고 너무 길어져도 실패 리턴
        }
    }
    uint32_t endTick = gpioTick();

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

    init_pca9685_safely(handle);

    UmbrellaSlot slots[NUM_SLOTS] = {
        {1, 24, 23, 0, 0, 0}, 
        {2, 17, 27, 1, 0, 0}, 
        {3, 5, 6, 2, 0, 0}, 
        {4,  9, 11, 3, 0, 0}  
    };

    for (int i = 0; i < NUM_SLOTS; i++) {
        gpioSetMode(slots[i].trig_pin, PI_OUTPUT);
        gpioSetMode(slots[i].echo_pin, PI_INPUT);
        gpioWrite(slots[i].trig_pin, 0); 
    }

    printf("=== 스마트 우산 보관함 4채널 고속 센싱 모드 가동 ===\n");
    gpioDelay(500000); 

    while (1) {
        char status_line[512] = ""; 
        char temp[64];

        for (int i = 0; i < NUM_SLOTS; i++) {
            
            if (slots[i].is_locked) {
                if (time(NULL) - slots[i].lock_time >= 5) {
                    printf("\n[INFO] S%d 5초 개방 종료 -> 대기 모드 원위치 복귀.\n", slots[i].slot_num);
                    
                    if (slots[i].slot_num == 1 || slots[i].slot_num == 3) {
                        set_pwm(handle, slots[i].servo_ch, 0, S13_LOCK_PULSE); 
                    } else {
                        set_pwm(handle, slots[i].servo_ch, 0, S24_LOCK_PULSE); 
                    }
                    gpioDelay(500000); 
                    set_pwm(handle, slots[i].servo_ch, 0, 0); 
                    
                    slots[i].is_locked = 0; 
                }
                sprintf(temp, "S%d:[OPEN] ", slots[i].slot_num);
                strcat(status_line, temp);
                continue; 
            }

            double dist = get_distance(slots[i].trig_pin, slots[i].echo_pin);

            if (dist > 1.0 && dist < 100.0) {
                if (dist >= 4.5 && dist <= 5.5) {
                    printf("\n[ALERT] S%d 우산 감지! 대칭 개방 구동.\n", slots[i].slot_num);
                    
                    gpioDelay(50000); 
                    
                    if (slots[i].slot_num == 1 || slots[i].slot_num == 3) {
                        set_pwm(handle, slots[i].servo_ch, 0, S13_OPEN_PULSE); 
                    } else {
                        set_pwm(handle, slots[i].servo_ch, 0, S24_OPEN_PULSE); 
                    }
                    
                    slots[i].is_locked = 1;        
                    slots[i].lock_time = time(NULL); 
                    
                    sprintf(temp, "S%d:[RUN] ", slots[i].slot_num);
                    strcat(status_line, temp);
                } else {
                    sprintf(temp, "S%d:%4.1fcm ", slots[i].slot_num, dist);
                    strcat(status_line, temp);
                }
            } else {
                sprintf(temp, "S%d:[ERR] ", slots[i].slot_num);
                strcat(status_line, temp);
            }
            // 💡 인접 센서 간의 초음파 잔향 간섭(Cross-talk)을 방지하기 위한 안전 마진 추가 조정
            gpioDelay(40000); 
        }

        printf("\r%s   ", status_line);
        fflush(stdout); 
        
        gpioDelay(50000); 
    }

    i2cClose(handle);
    gpioTerminate();
    return 0;
}
