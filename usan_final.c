#include <pigpio.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <curl/curl.h>
#include <json-c/json.h>

#define PCA9685_ADDR 0x40 
#define MODE1        0x00
#define PRESCALE     0xFE
#define LED0_ON_L    0x06
#define NUM_SLOTS    4

#define FIREBASE_URL "https://umbrella-ee957-default-rtdb.firebaseio.com/slots.json"
#define FIREBASE_SLOT_URL_FORMAT "https://umbrella-ee957-default-rtdb.firebaseio.com/slots/slot%d.json"

#define S13_LOCK_PULSE    200   
#define S13_OPEN_PULSE    450   
#define S24_LOCK_PULSE    450   
#define S24_OPEN_PULSE    200   

typedef enum {
    IDLE = 0,
    WAIT_IN,           
    WAIT_OUT,          
    PENDING_LOCK_IN,   
    PENDING_LOCK_OUT   
} SlotState;

typedef struct {
    int slot_num, trig_pin, echo_pin, servo_ch;
    SlotState state;
    time_t start_time;
    time_t pending_time;
    char expected_final_status[16]; 
    char current_user[64];          
    char current_status[16];        
} UmbrellaSlot;

UmbrellaSlot slots[NUM_SLOTS] = {
    {1, 24, 23, 0, IDLE, 0, 0, "", "", "READY"}, 
    {2, 17, 27, 1, IDLE, 0, 0, "", "", "READY"},
    {3, 5,  6,  2, IDLE, 0, 0, "", "", "READY"}, 
    {4, 9,  11, 3, IDLE, 0, 0, "", "", "READY"}
};

struct MemoryStruct { char *memory; size_t size; };

static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;
    char *ptr = realloc(mem->memory, mem->size + realsize + 1);
    if(!ptr) return 0;
    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;
    return realsize;
}

void update_firebase(int slot_num, const char* status, const char* user) {
    CURL *curl = curl_easy_init();
    if(curl) {
        char url[256], json[256];
        sprintf(url, FIREBASE_SLOT_URL_FORMAT, slot_num);
        if(user == NULL || strlen(user) == 0) sprintf(json, "{\"status\": \"%s\", \"currentUser\": \"\"}", status);
        else sprintf(json, "{\"status\": \"%s\", \"currentUser\": \"%s\"}", status, user);
        
        struct curl_slist *headers = curl_slist_append(NULL, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PATCH");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json);
        curl_easy_perform(curl);
        curl_slist_free_all(headers); curl_easy_cleanup(curl);
    }
}

void set_pwm(int handle, int channel, int off) {
    int reg = LED0_ON_L + (channel * 4);
    i2cWriteByteData(handle, reg + 2, off & 0xFF);
    i2cWriteByteData(handle, reg + 3, off >> 8);
}

double get_dist(int trig, int echo) {
    gpioWrite(trig, 1); gpioDelay(10); gpioWrite(trig, 0);
    uint32_t s = gpioTick(); 
    while(gpioRead(echo) == 0) if(gpioTick() - s > 3000) return -1;
    uint32_t start = gpioTick(); 
    while(gpioRead(echo) == 1);
    return (double)(gpioTick() - start) / 58.0;
}

int main(void) {
    if (gpioInitialise() < 0) return -1;
    
    int handle = i2cOpen(1, PCA9685_ADDR, 0);
    if (handle < 0) {
        printf("❌ PCA9685 I2C 연결 실패!\n");
        return -1;
    }

    i2cWriteByteData(handle, MODE1, 0x10); 
    gpioDelay(50000); 
    i2cWriteByteData(handle, PRESCALE, 121); 
    i2cWriteByteData(handle, MODE1, 0x20); 
    gpioDelay(50000); 

    curl_global_init(CURL_GLOBAL_ALL);

    for (int i = 0; i < NUM_SLOTS; i++) {
        gpioSetMode(slots[i].trig_pin, PI_OUTPUT);
        gpioSetMode(slots[i].echo_pin, PI_INPUT);
    }
    
    printf("\n=== 🛡️ 스마트 우산 거치대 메인 제어 시스템 가동 ===\n");

    while(1) {
        struct MemoryStruct chunk = {malloc(1), 0};
        CURL *curl = curl_easy_init();
        curl_easy_setopt(curl, CURLOPT_URL, FIREBASE_URL);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &chunk);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
        curl_easy_perform(curl); curl_easy_cleanup(curl);

        struct json_object *root = json_tokener_parse(chunk.memory);
        free(chunk.memory);

        for(int i = 0; i < NUM_SLOTS; i++) {
            char key[16]; sprintf(key, "slot%d", slots[i].slot_num);
            struct json_object *s_obj, *st, *us;
            const char *status = "READY", *user = "";
            
            if(root && json_object_object_get_ex(root, key, &s_obj)) {
                if(json_object_object_get_ex(s_obj, "status", &st)) status = json_object_get_string(st);
                if(json_object_object_get_ex(s_obj, "currentUser", &us)) user = json_object_get_string(us);
            }

            strncpy(slots[i].current_status, status, 15);
            strncpy(slots[i].current_user, user, 63);

            if(slots[i].state != IDLE) {
                double d = get_dist(slots[i].trig_pin, slots[i].echo_pin);
                time_t now = time(NULL);

                if(slots[i].state == WAIT_IN) {
                    if(d > 0 && d <= 8.0) {
                        slots[i].state = PENDING_LOCK_IN;
                        slots[i].pending_time = now;
                        printf("슬롯 %d번 우산 감지! 5초 후 잠깁니다.\n", slots[i].slot_num);
                    } else if(now - slots[i].start_time >= 10) {
                        printf("슬롯 %d번 IN 시간 초과.\n", slots[i].slot_num);
                        set_pwm(handle, slots[i].servo_ch, (slots[i].slot_num%2==0) ? S24_LOCK_PULSE : S13_LOCK_PULSE);
                        update_firebase(slots[i].slot_num, "READY", "");
                        slots[i].state = IDLE;
                    }
                }
                else if(slots[i].state == WAIT_OUT) {
                    if(d > 20.0 || d < 0) {
                        slots[i].state = PENDING_LOCK_OUT;
                        slots[i].pending_time = now;
                        printf("슬롯 %d번 우산 빼냄 감지! 5초 후 잠깁니다.\n", slots[i].slot_num);
                    } else if(now - slots[i].start_time >= 10) {
                        printf("슬롯 %d번 OUT 시간 초과.\n", slots[i].slot_num);
                        set_pwm(handle, slots[i].servo_ch, (slots[i].slot_num%2==0) ? S24_LOCK_PULSE : S13_LOCK_PULSE);
                        
                        // 대여 실패(시간 초과) 시 원래 우산이 꽂혀있던 대기 상태(READY)로 복구
                        const char* rollback = (strcmp(slots[i].expected_final_status, "RENT") == 0) ? "READY" : "USING";
                        update_firebase(slots[i].slot_num, rollback, slots[i].current_user); 
                        slots[i].state = IDLE;
                    }
                }
                else if(slots[i].state == PENDING_LOCK_IN || slots[i].state == PENDING_LOCK_OUT) {
                    if(now - slots[i].pending_time >= 5) {
                        set_pwm(handle, slots[i].servo_ch, (slots[i].slot_num%2==0) ? S24_LOCK_PULSE : S13_LOCK_PULSE);
                        update_firebase(slots[i].slot_num, slots[i].expected_final_status, slots[i].current_user);
                        slots[i].state = IDLE;
                    }
                }
            } 
            else {
                // 💡 [핵심 예외 처리] 현재 DB 상태가 RENT라면, 사용자가 웹에서 반납할 때까지 아무것도 안 하고 유지!
                if(strcmp(status, "RENT") == 0) {
                    continue; 
                }

                if(strcmp(status, "IN_STORE") == 0 || strcmp(status, "IN_RETURN") == 0) {
                    printf("슬롯 %d번 IN 명령 수신 (%s)\n", slots[i].slot_num, status);
                    update_firebase(slots[i].slot_num, "OPEN", user); 
                    set_pwm(handle, slots[i].servo_ch, (slots[i].slot_num%2==0) ? S24_OPEN_PULSE : S13_OPEN_PULSE);
                    slots[i].state = WAIT_IN;
                    slots[i].start_time = time(NULL);
                    strcpy(slots[i].expected_final_status, (strcmp(status, "IN_STORE") == 0) ? "USING" : "READY");
                    strncpy(slots[i].current_user, user, 63);
                }
                else if(strcmp(status, "OUT_RENT") == 0 || strcmp(status, "OUT_RECEIVE") == 0) {
                    printf("슬롯 %d번 OUT 명령 수신 (%s)\n", slots[i].slot_num, status);
                    update_firebase(slots[i].slot_num, "OPEN", user); 
                    set_pwm(handle, slots[i].servo_ch, (slots[i].slot_num%2==0) ? S24_OPEN_PULSE : S13_OPEN_PULSE);
                    slots[i].state = WAIT_OUT;
                    slots[i].start_time = time(NULL);
                    
                    strcpy(slots[i].expected_final_status, (strcmp(status, "OUT_RENT") == 0) ? "RENT" : "READY");
                    strncpy(slots[i].current_user, user, 63);
                }
            }
        }
        
        if(root) json_object_put(root);
        gpioDelay(200000); 
    }
}
