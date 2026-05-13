#include <gpiod.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

int main(void) {
    const char *chip_path = "/dev/gpiochip4"; // 3B+에서 안되면 /dev/gpiochip0 시도
    unsigned int offset = 17;                 // GPIO 17
    struct gpiod_chip *chip;
    struct gpiod_line_settings *settings;
    struct gpiod_line_config *line_cfg;
    struct gpiod_request_config *req_cfg;
    struct gpiod_line_request *request;
    int ret;

    // 1. 칩 열기
    chip = gpiod_chip_open(chip_path);
    if (!chip) {
        perror("GPIO 칩 열기 실패");
        return 1;
    }

    // 2. 라인 설정 (출력 모드)
    settings = gpiod_line_settings_new();
    gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_OUTPUT);

    line_cfg = gpiod_line_config_new();
    gpiod_line_config_add_line_settings(line_cfg, &offset, 1, settings);

    // 3. 요청 설정 (소비자 이름 등록)
    req_cfg = gpiod_request_config_new();
    gpiod_request_config_set_consumer(req_cfg, "led_test_v2");

    // 4. GPIO 요청
    request = gpiod_chip_request_lines(chip, req_cfg, line_cfg);
    if (!request) {
        perror("GPIO 요청 실패");
        goto show_help;
    }

    printf("libgpiod v2로 LED 제어 시작! (GPIO 17)\n");

    for (int i = 0; i < 10; i++) {
        // LED ON (1)
        gpiod_line_request_set_value(request, offset, GPIOD_LINE_VALUE_ACTIVE);
        printf("LED ON\n");
        sleep(1);

        // LED OFF (0)
        gpiod_line_request_set_value(request, offset, GPIOD_LINE_VALUE_INACTIVE);
        printf("LED OFF\n");
        sleep(1);
    }

    // 리소스 해제
    gpiod_line_request_release(request);
show_help:
    gpiod_line_config_free(line_cfg);
    gpiod_line_settings_free(settings);
    gpiod_request_config_free(req_cfg);
    gpiod_chip_close(chip);

    return 0;
}