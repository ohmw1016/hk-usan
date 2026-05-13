#include <gpiod.h>
#include <stdio.h>
#include <unistd.h>

#define VIB_PIN 17  // 진동 센서 DO 연결 (BCM 17)
#define LED_PIN 27  // 알람 표시용 LED (BCM 27)

int main(void) {
    struct gpiod_chip *chip = gpiod_chip_open("/dev/gpiochip0");
    if (!chip) { perror("Chip open failed"); return 1; }

    // 1. 라인 설정 (진동 센서: 입력, LED: 출력)
    struct gpiod_line_settings *settings = gpiod_line_settings_new();
    struct gpiod_line_config *line_cfg = gpiod_line_config_new();

    // LED 설정
    unsigned int led_offset = LED_PIN;
    gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_OUTPUT);
    gpiod_line_config_add_line_settings(line_cfg, &led_offset, 1, settings);

    // 진동 센서 설정 (양방향 에지 검출 설정)
    unsigned int vib_offset = VIB_PIN;
    gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_INPUT);
    gpiod_line_settings_set_edge_detection(settings, GPIOD_LINE_EDGE_BOTH); 
    gpiod_line_config_add_line_settings(line_cfg, &vib_offset, 1, settings);

    struct gpiod_request_config *req_cfg = gpiod_request_config_new();
    gpiod_request_config_set_consumer(req_cfg, "vibration_alarm");

    struct gpiod_line_request *request = gpiod_chip_request_lines(chip, req_cfg, line_cfg);
    if (!request) { perror("Request lines failed"); return 1; }

    printf("=== 보안 시스템 가동: 진동 감지 시 LED 점등 ===\n");

    struct gpiod_edge_event_buffer *buffer = gpiod_edge_event_buffer_new(16);

    while (1) {
        // 인터럽트 이벤트 대기 (대기 시간 제한 없음)
        if (gpiod_line_request_wait_edge_events(request, -1) > 0) {
            gpiod_line_request_read_edge_events(request, buffer, 16);
            
            printf("[ALERT] 외부 충격 감지! 보안 위협 발생!\n");
            
            // LED 0.5초간 점등 (경고 표시)
            gpiod_line_request_set_value(request, LED_PIN, 1);
            usleep(500000); 
            gpiod_line_request_set_value(request, LED_PIN, 0);
        }
    }

    gpiod_line_request_release(request);
    gpiod_chip_close(chip);
    return 0;
}