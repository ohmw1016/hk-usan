import cv2
import firebase_admin
from firebase_admin import credentials
from firebase_admin import db
from pyzbar.pyzbar import decode
import subprocess
import numpy as np
import time
import sys

# =================================================================
# 🌐 1. Firebase 인증 연동
# =================================================================
try:
    cred = credentials.Certificate("/home/usan/code/firebase_key.json")
    firebase_admin.initialize_app(cred, {
        'databaseURL': 'https://umbrella-ee957-default-rtdb.firebaseio.com'
    })
    print("[SYSTEM] Firebase 실시간 데이터베이스 연동 성공!", flush=True)
except Exception as e:
    print(f"[ERROR] Firebase 초기화 실패: {e}", flush=True)
    sys.exit(1)


# =================================================================
# 🎯 2. QR 데이터 분할 및 4가지 동작 상태 주입
# =================================================================
def verify_and_assign_slot(qr_data):
    raw_text = qr_data.strip().replace("[", "").replace("]", "")
    print(f"\n[QR DETECTED] 🔍 QR 스캔 성공 -> 정제된 데이터: [{raw_text}]", flush=True)
    
    try:
        parsed_data = raw_text.split('/')
        
        if len(parsed_data) < 3:
            print(f"[DENIED] ❌ QR 데이터 포맷이 올바르지 않습니다 (슬래시 부족): [{raw_text}]", flush=True)
            return

        action_raw = parsed_data[0].strip()   # "보관", "반납", "대여", "수령"
        slot_raw = parsed_data[1].strip()     # "slots3" 또는 "slot3"
        user_id = parsed_data[2].strip()      # "45" (유저 ID)

        # 💡 [핵심 로직] 동작에 따른 Firebase 주입 상태 및 유저 ID 유지/삭제 분기
        if action_raw == "보관":
            new_status = "IN_STORE"
            final_user = user_id
        elif action_raw == "반납":
            new_status = "IN_RETURN"
            final_user = ""
        elif action_raw == "대여":
            new_status = "OUT_RENT"
            final_user = user_id
        elif action_raw == "수령":
            new_status = "OUT_RECEIVE"
            final_user = ""
        else:
            print(f"[DENIED] ❌ 알 수 없는 동작명입니다: {action_raw}", flush=True)
            return

        # 슬롯 번호 정교화
        slot_clean = slot_raw.replace(" ", "")
        if "slots" in slot_clean:
            slot_key = slot_clean.replace("slots", "slot")
        else:
            slot_key = slot_clean

        db_path = f'slots/{slot_key}'

        print(f"=======================================================")
        print(f"🎉 [QR 분석 완료] {action_raw} 프로세스 가동")
        print(f"🚪 타겟 슬롯 주소 : {db_path}")
        print(f"👤 주입 유저 정보 : '{final_user}'")
        print(f"=======================================================", flush=True)

        slot_ref = db.reference(db_path)
        slot_ref.update({
            "currentUser": final_user,
            "status": new_status
        })
        print(f"[SYSTEM] 🚀 {db_path} ➡️ currentUser: '{final_user}', status: '{new_status}' 업데이트 완료!!", flush=True)
            
    except Exception as e:
        print(f"[SERVER ERROR] QR 데이터 분석 및 DB 주입 중 에러 발생: {e}\n", flush=True)


# =================================================================
# 🖼️ 3. 이미지 전처리 함수
# =================================================================
def preprocess(img):
    gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
    clahe = cv2.createCLAHE(clipLimit=2.0, tileGridSize=(8, 8))
    enhanced = clahe.apply(gray)
    blurred = cv2.GaussianBlur(enhanced, (3, 3), 0)
    return blurred

WIDTH, HEIGHT = 640, 480

# =================================================================
# 📷 4. 메인 카메라 구동 루프
# =================================================================
def main():
    print("\n=== 시스템 가동: rpicam subprocess + pyzbar QR 디텍터 ===", flush=True)

    rpicam_cmd = [
        "rpicam-vid", "-t", "0",
        "--width", str(WIDTH), "--height", str(HEIGHT),
        "--framerate", "15",
        "--codec", "mjpeg",
        "-o", "-"
    ]
    ffmpeg_cmd = [
        "ffmpeg", "-i", "pipe:0",
        "-f", "rawvideo", "-pix_fmt", "bgr24",
        "-s", f"{WIDTH}x{HEIGHT}",
        "pipe:1"
    ]

    try:
        rpicam_proc = subprocess.Popen(rpicam_cmd, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        ffmpeg_proc = subprocess.Popen(ffmpeg_cmd, stdin=rpicam_proc.stdout, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    except Exception as e:
        print(f"[ERROR] 카메라 서브프로세스 파이프 가동 실패: {e}")
        return

    print("\n=====================================================")
    print("★ QR 스캔 엔진 감시 중: 카메라 렌즈에 QR 코드를 비춰주세요 ★")
    print("=====================================================", flush=True)

    frame_size = WIDTH * HEIGHT * 3
    frame_count = 0
    last_pushed_qr = None  

    try:
        while True:
            raw = ffmpeg_proc.stdout.read(frame_size)
            if len(raw) != frame_size:
                time.sleep(0.01)
                continue

            img = np.frombuffer(raw, dtype=np.uint8).reshape((HEIGHT, WIDTH, 3))
            frame_count += 1

            processed = preprocess(img)
            qr_codes = decode(processed)
            detected_in_this_frame = False

            if qr_codes:
                for qr in qr_codes:
                    data = qr.data.decode('utf-8')
                    detected_in_this_frame = True
                    
                    if data != last_pushed_qr:
                        verify_and_assign_slot(data)
                        last_pushed_qr = data  
                    else:
                        if frame_count % 15 == 0:
                            print("[SYSTEM] (동일한 QR 스캔 중복 차단 대기 중)", flush=True)

            if not detected_in_this_frame:
                if last_pushed_qr is not None:
                    print("\n[SYSTEM] 🔄 QR 코드가 제거되었습니다. 다음 스캔 준비 완료.", flush=True)
                last_pushed_qr = None  

            if frame_count % 30 == 0 and not detected_in_this_frame:
                print("[LIVE] 스캐너 정상 작동 중... QR 코드를 비춰주세요.", flush=True)

            time.sleep(0.01)

    except KeyboardInterrupt:
        print("\n[SYSTEM] 사용자에 의해 QR 스캐너 프로그램이 종료되었습니다.")
    finally:
        try:
            ffmpeg_proc.terminate()
            rpicam_proc.terminate()
        except:
            pass
        sys.exit(0)

if __name__ == "__main__":
    main()
