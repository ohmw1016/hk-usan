#include <gpiod.h>
#include <stdio.h>
#include <unistd.h>

int main(void) {
    // 3B+에서 확인된 실제 GPIO 칩 경로
    struct gpiod_chip *chip = gpiod_chip_open("/dev/gpiochip0");
    if (!chip) {
        perror("칩 오픈 실패");
        return 1;
    }

    unsigned int offset = 23; // PIR 센서가 연결된 GPIO 번호

    struct gpiod_line_settings *settings = gpiod_line_settings_new();
    gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_INPUT);

    struct gpiod_line_config *line_cfg = gpiod_line_config_new();
    gpiod_line_config_add_line_settings(line_cfg, &offset, 1, settings);

    struct gpiod_request_config *req_cfg = gpiod_request_config_new();
    gpiod_request_config_set_consumer(req_cfg, "sensor_check");

    struct gpiod_line_request *request = gpiod_chip_request_lines(chip, req_cfg, line_cfg);
    if (!request) {
        perror("라인 요청 실패");
        return 1;
    }

    printf("센서 모니터링 시작 (종료: Ctrl+C)\n");
    printf("나사를 시계 방향으로 돌려 0이 나오게 만든 후 테스트하세요.\n");

   while (1) {
    int val = gpiod_line_request_get_value(request, offset);
    
    if (val == 1) {
        // [필터링] 0.3초 동안 계속 1을 유지할 때만 "진짜 사람"으로 인정
        int count = 0;
        for(int i=0; i<3; i++) {
            usleep(100000); // 0.1초씩 확인
            if(gpiod_line_request_get_value(request, offset) == 1) count++;
        }

        if (count >= 2) { // 0.3초 중 0.2초 이상 감지되면
            printf("!!! 확실한 움직임 포착 !!!\n");
            // 여기서 모터 구동 함수 실행
            sleep(5); // 한 번 감지 후엔 센서가 진정되도록 5초간 휴식
        }
    }
    usleep(100000);
}

    return 0;
}