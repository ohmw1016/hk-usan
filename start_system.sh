#!/bin/bash

# 💡 크론탭 환경에서도 라이브러리를 정상적으로 찾도록 경로 강제 지정
export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH

# 💡 확실하게 폴더 지정
cd /home/usan/code

echo "=================================================="
echo "🛡️ 스마트 우산 거치대 통합 자동화 시스템 가동 시작"
echo "=================================================="

# 💡 검증된 실제 파일명과 경로 매칭
sudo /home/usan/code/usan_final > /home/usan/code/usan_final.log 2>&1 &
sudo /home/usan/code/sw_db > /home/usan/code/sw_db.log 2>&1 &
python3 /home/usan/code/qr_reader.py > /home/usan/code/qr_reader.log 2>&1 &

echo "=================================================="
echo "✅ 시스템 가동 명령 완료!"
echo "=================================================="
