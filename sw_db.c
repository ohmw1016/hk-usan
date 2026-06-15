#include <gpiod.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <curl/curl.h>
#include <json-c/json.h>

#define NUM_SLOTS 4
#define FIREBASE_SLOT_URL_FORMAT "https://umbrella-ee957-default-rtdb.firebaseio.com/slots/slot%d.json"
#define FIREBASE_USER_URL_FORMAT "https://umbrella-ee957-default-rtdb.firebaseio.com/users/%s.json"

typedef struct {
    int slot_num;
    unsigned int vib_pin; 
    int count;            
    uint64_t first_time_ns; // 💡 최초 타격 시점 (나노초)
    uint64_t last_time_ns;  // 💡 마지막 타격 시점 (채터링 방지용)
} VibSlot;

VibSlot slots[NUM_SLOTS] = {
    {1, 21, 0, 0, 0}, // 💡 1번 슬롯: 21번 핀으로 교체
    {2, 19, 0, 0, 0}, 
    {3, 26, 0, 0, 0}, 
    {4, 13, 0, 0, 0}  // 💡 4번 슬롯: 13번 핀으로 교체
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

// 💡 성공한 로직: 따옴표 제거하여 불린(Boolean) 값으로 전송
void send_alarm_state_to_user(const char* user_id, const char* state) {
    CURL *curl = curl_easy_init();
    if(curl) {
        char user_url[256];
        char json_data[64];
        
        sprintf(user_url, FIREBASE_USER_URL_FORMAT, user_id);
        sprintf(json_data, "{\"vibration_alert\": %s}", state);

        struct curl_slist *headers = curl_slist_append(NULL, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_URL, user_url);
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PATCH");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_data);
        
        CURLcode res = curl_easy_perform(curl);
        if(res == CURLE_OK) {
            printf("✅ [전송] %s 알람 상태 -> %s\n", user_id, state);
        }
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }
}

// 💡 성공한 로직: 1초 대기 후 리셋 적용
void fetch_user_and_send_alarm(int slot_num) {
    char slot_url[256];
    sprintf(slot_url, FIREBASE_SLOT_URL_FORMAT, slot_num);
    struct MemoryStruct chunk = {malloc(1), 0};

    CURL *curl = curl_easy_init();
    if(curl) {
        curl_easy_setopt(curl, CURLOPT_URL, slot_url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
        curl_easy_perform(curl);
        curl_easy_cleanup(curl);
    }

    if(chunk.size > 0) {
        struct json_object *parsed_json = json_tokener_parse(chunk.memory);
        struct json_object *user_obj;
        
        if(parsed_json && json_object_object_get_ex(parsed_json, "currentUser", &user_obj)) {
            const char *user_id = json_object_get_string(user_obj);
            
            if(user_id != NULL && strlen(user_id) > 0) {
                // [1단계] TRUE 전송
                send_alarm_state_to_user(user_id, "true");
                
                // 앱이 변화를 감지할 시간(1초)
                sleep(1); 
                
                // [2단계] FALSE 전송하여 리셋
                send_alarm_state_to_user(user_id, "false");
                printf("🔄 즉시 리셋 완료.\n");
            }
        }
        if(parsed_json) json_object_put(parsed_json); 
    }
    free(chunk.memory); 
}

int main(void) {
    curl_global_init(CURL_GLOBAL_ALL);
    struct gpiod_chip *chip = gpiod_chip_open("/dev/gpiochip0");
    if (!chip) return 1; 

    struct gpiod_line_settings *settings = gpiod_line_settings_new();
    struct gpiod_line_config *line_cfg = gpiod_line_config_new();
    gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_INPUT);
    gpiod_line_settings_set_edge_detection(settings, GPIOD_LINE_EDGE_RISING); 

    unsigned int offsets[NUM_SLOTS];
    for(int i = 0; i < NUM_SLOTS; i++) offsets[i] = slots[i].vib_pin;
    gpiod_line_config_add_line_settings(line_cfg, offsets, NUM_SLOTS, settings);

    struct gpiod_request_config *req_cfg = gpiod_request_config_new();
    struct gpiod_line_request *request = gpiod_chip_request_lines(chip, req_cfg, line_cfg);
    struct gpiod_edge_event_buffer *buffer = gpiod_edge_event_buffer_new(64);

    printf("=== 🛡️ 완벽 가동: 디바운싱(둔감화) + 1초 알람 리셋 적용 ===\n");
    printf("3초 안에 5번 충격이 발생해야만 알람이 전송됩니다.\n\n");

    while (1) {
        if (gpiod_line_request_wait_edge_events(request, -1) > 0) {
            int num_events = gpiod_line_request_read_edge_events(request, buffer, 64);
            
            for (int i = 0; i < num_events; i++) {
                struct gpiod_edge_event *event = gpiod_edge_event_buffer_get_event(buffer, i);
                unsigned int triggered_pin = gpiod_edge_event_get_line_offset(event);
                
                // 💡 시간(나노초) 가져오기
                uint64_t ts = gpiod_edge_event_get_timestamp_ns(event);
                
                for (int j = 0; j < NUM_SLOTS; j++) {
                    if (slots[j].vib_pin == triggered_pin) {
                        
                        // 💡 1. 채터링 방지: 이전 타격 후 0.1초(100ms) 이내의 신호는 기계적 떨림으로 간주하고 무시
                        if (ts - slots[j].last_time_ns < 100000000ULL) {
                            break; 
                        }
                        slots[j].last_time_ns = ts;

                        // 💡 2. 3초 시간 제한: 3초(3,000,000,000ns)가 지났으면 카운트 초기화
                        if (slots[j].count == 0 || (ts - slots[j].first_time_ns) > 3000000000ULL) {
                            slots[j].count = 1;
                            slots[j].first_time_ns = ts;
                            printf("🔨 슬롯 %d번 1타 감지!\n", slots[j].slot_num);
                        } else {
                            // 3초 내에 들어온 정상적인 타격이면 카운트 증가
                            slots[j].count++;
                            printf("🔨 슬롯 %d번 %d타 누적!\n", slots[j].slot_num, slots[j].count);
                        }

                        // 💡 3. 알람 발생: 5타 누적 시 앱으로 전송
                        if (slots[j].count >= 5) {
                            printf("\n[🚨 ALARM] 슬롯 %d번 도난/강한 충격 확정!!\n", slots[j].slot_num);
                            
                            // 통신 함수 호출
                            fetch_user_and_send_alarm(slots[j].slot_num);
                            
                            // 전송 후 카운트 리셋
                            slots[j].count = 0; 
                        }
                        break;
                    }
                }
            }
        }
    }
    return 0;
}
