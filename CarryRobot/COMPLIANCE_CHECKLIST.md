# Kiểm tra đáp ứng yêu cầu vận hành – Carry Robot

**Đã patch (cập nhật):** Xuất phát SW+MED, tới đích 180°+line check, trở về chờ SW, kết thúc 180° tại MED, Servo Y nghỉ chế độ 1, OLED "Press SW to Return" / "Braking...".

---

## CHẾ ĐỘ 1: TỰ HÀNH GIAO HÀNG (Autonomous Mode)

### Yêu cầu: Vạch line luôn ở giữa xe
| # | Yêu cầu | Trạng thái | Ghi chú |
|---|---------|------------|---------|
| 1.1 | Line phải luôn ở giữa xe (PID dò line) | ✅ Đáp ứng | Slave: `line_follower.cpp` PID 3 cảm biến, error → correction. Master gửi baseSpeed + turnCmd (hoặc Slave tự điều khiển trong chế độ delegated). |

### Quản lý phần cứng – Chế độ 1
| # | Yêu cầu | Trạng thái | Ghi chú |
|---|---------|------------|---------|
| 1.2 | TẮT: Siêu âm 2 bên | ⚠️ Một phần | Siêu âm chỉ dùng trong `updateFollow()` (Follow mode). Trong OUTBOUND/BACK/MISSION_DELEGATED không gọi `usLeftMm()`/`usRightMm()` cho điều khiển → không tắt hẳn, chỉ không dùng. |
| 1.3 | TẮT: Camera HuskyLens | ⚠️ Một phần | HuskyLens vẫn chạy `huskyMaintain()` mọi state; không tắt nguồn. Trong OUTBOUND/BACK không gọi `huskyGetTarget()`/`huskyGetLine()` → không dùng cho điều khiển. |
| 1.4 | TẮT: Servo Y (nghỉ) | ❌ Chưa đáp ứng | Trong `enterOutbound()`/`enterBack()` không gọi `gimbalSetY()`. Servo Y giữ góc lần set cuối (LEVEL hoặc TILT_DOWN). Cần thêm: khi vào OUTBOUND/BACK đặt Servo Y về góc “nghỉ” (ví dụ LEVEL 90°). |
| 1.5 | BẬT: Dò Line 5 mắt | ⚠️ Khác thiết kế | Code dùng **3 cảm biến** (LINE_S1, S2, S3 – Left, Centre, Right). Comment Slave config: "Using the middle 3 sensors from the previous 5-sensor harness". Phần cứng có thể có 5 mắt nhưng firmware chỉ dùng 3. |
| 1.6 | BẬT: Thẻ từ PN532 (ESP32) | ✅ Đáp ứng | Slave: `rfid_reader.cpp`, `enableRFID=1` khi line-follow. |
| 1.7 | BẬT: ToF (né vật cản) | ✅ Đáp ứng | Master: `checkObstacle()` trong OUTBOUND/BACK/MISSION_DELEGATED, ToF < TOF_STOP_DIST → ST_OBSTACLE, còi. |

### Kịch bản 1 chuyến đi
| # | Yêu cầu | Trạng thái | Ghi chú |
|---|---------|------------|---------|
| 1.8 | Chờ lệnh: Đỗ tại MED, OLED IDLE | ✅ Đáp ứng | `displayIdle()`: "--- STANDBY ---" / "Waiting for Order...". |
| 1.9 | Nhận lệnh: Web gửi JSON, OLED đổi tên bệnh nhân | ✅ Đáp ứng | `routeParseAssign()` lưu patientName, destBed; khi có mission `displayIdle()` hiện "MISSION READY", Patient, Bed. **Lưu ý:** Luồng mới (delegated) sau khi assign **không** chờ SW+MED mà gửi route xuống Slave và start ngay → OLED chuyển "DELIVERING..." / "Slave running...". |
| 1.10 | Xuất phát: Y tá bỏ đồ → **Bấm SW 1 lần + Quét thẻ MED** → Xe khởi hành (ST_OUTBOUND) | ✅ Đáp ứng (đã patch) | mission/assign chỉ gửi route xuống Slave (missionDelegateSendRoutesOnly). Khi quét thẻ MED trong IDLE → medCardScanned=true. Bấm SW 1 lần khi đã quét MED → missionDelegateStartMission() gửi missionStart và vào ST_MISSION_DELEGATED. Nếu chưa quét MED mà bấm SW → beep "Scan MED first". |
| 1.11 | Di chuyển: Slave đọc UID, gửi ESP-NOW cho Master; Master đối chiếu JSON ra lệnh rẽ; ToF vật cản → phanh + còi | ✅ Đáp ứng | Slave báo RFID; Master (khi không delegated) so expectedNextUid → turnCmd L/R/B. Delegated: Slave tự so UID trong route_runner. ToF → ST_OBSTACLE, buzzerObstacle(). |
| 1.12 | Tới đích: Cán thẻ R1M1 → Phanh → **Xoay 180°** → **Delay/kiểm tra line bám vạch** → WAIT AT DEST, còi | ✅ Đáp ứng (đã patch) | Slave: khi tới node cuối outbound → SL_AT_DEST_UTURN: executeTurn('B'), sau đó chờ LINE_REACQUIRE_MS (500ms) hoặc lineDetected() rồi mới report COMPLETE và chuyển SL_WAIT_AT_DEST. Master nhận status 2 → displayWaitAtDest(), smSetWaitingAtDest(true). |
| 1.13 | Trở về: **Bác sĩ nhận đồ xong → Bấm SW 1 lần** → ST_BACK | ✅ Đáp ứng (đã patch) | Slave không tự chuyển BACK sau 2s. SL_WAIT_AT_DEST chỉ chuyển sang SL_BACK khi nhận startReturn từ Master. Master: khi slaveReport.missionStatus==2 set smSetWaitingAtDest(true); khi user bấm SW 1 lần trong MISSION_DELEGATED và waitingAtDestForSw → gửi masterMsg.startReturn=1, sau đó clear flag. Slave processMasterCmd khi startReturn gọi routeRunnerOnStartReturn() → s_startReturnRequested, routeRunnerUpdate chuyển sang SL_BACK. |
| 1.14 | Kết thúc: Về MED → Cán thẻ → **Xoay 180° mặt ra ngoài** → Xóa lộ trình → IDLE | ✅ Đáp ứng (đã patch) | Slave: khi tới node cuối return (MED) → SL_AT_MED_UTURN: executeTurn('B') (180°), rồi report BACK và SL_IDLE. Master nhận status 3 → mqttSendReturned(), smEnterIdle() (routeClear, IDLE). |

---

## CHẾ ĐỘ 2: BÁM ĐUÔI (Follow Person Mode)

| # | Yêu cầu | Trạng thái | Ghi chú |
|---|---------|------------|---------|
| 2.1 | Kích hoạt: **Bấm đúp SW** khi rảnh | ❌ Khác | Code: **Long press** (3s) SW khi IDLE → enterFollow(). Doc: "Bấm đúp SW". Cần thêm nhận diện double-click (2 lần click nhanh) thay hoặc bên cạnh long press. |
| 2.2 | TẮT: Dò Line, PN532. BẬT: HuskyLens, Servo Y, 2 Siêu âm, ToF | ✅ Đáp ứng | enterFollow(): enableLine=0, enableRFID=0; gimbalSetY(SERVO_Y_LEVEL); updateFollow() dùng HuskyLens, ToF, usLeftMm/usRightMm. |
| 2.3 | Lệch tâm → trượt (strafe); Servo X quay tìm người → xe quay theo; camera chỉ thẳng mục tiêu; khóa 90° khi bình thường, mất dấu mới mở khóa | ✅ Đáp ứng | follow_pid: pixel error → servo X (khi không lock); angle error → vY, vR; khi gần 90° gimbalLockX(true); mất dấu LOST_TIMEOUT_MS → searching, unlock servo, sweep. |
| 2.4 | Tracking, mất dấu & tìm kiếm, tái đồng bộ | ✅ Đáp ứng | followPidUpdate: targetSeen → track; else → sweep vR + servo; khi thấy lại → targetLocked, tiếp tục theo. |

---

## CHẾ ĐỘ 3: PHỤC HỒI & QUAY VỀ (Recovery Mode)

| # | Yêu cầu | Trạng thái | Ghi chú |
|---|---------|------------|---------|
| 3.1 | Kích hoạt: Bấm đúp SW khi đang follow → trả về tự hành | ❌ Khác | Code: Long press SW khi FOLLOW → enterIdle(). Doc: "Bấm đúp SW". Cùng vấn đề double-click vs long-press. |
| 3.2 | Bước 1 – Visual Docking: HuskyLens Line, Servo Y 45°, Mecanum trượt/xoay đến khi 5 mắt đọc vạch, xe giữa vạch | ✅ Đáp ứng | enterRecoveryVis(): gimbalSetY(SERVO_Y_TILT_DOWN); updateRecoveryVis() dùng huskyGetLine() → vY, vR, vX. Slave báo sync_docking (centre sensor) → enterRecoveryBlind(). |
| 3.3 | Bước 2 – Blind Run: Servo Y ngóc lên; TẮT Camera + Siêu âm; BẬT PN532; Master gửi "Sync" khi line khớp cho Slave chuyển PID Line | ✅ Đáp ứng | enterRecoveryBlind(): gimbalSetY(SERVO_Y_LEVEL). Slave sync_docking → Master enterRecoveryBlind(); Master enableLine=1, enableRFID=1. Sync = slaveReport.sync_docking (centre line sensor). |
| 3.4 | Bước 3 – Call Home: Chạy line, 2 checkpoint RFID, J4 phanh, MQTT "ở J4 xin đường về MED", Web gửi return route, ST_BACK | ✅ Đáp ứng | recoveryCheckpointsHit >= 2 → enterRecoveryCall(); nhận mission/return_route → enterBack(false). position/waiting_return đã có trong MQTT. |

---

## OLED – Nội dung theo trạng thái

| Trạng thái | Header (doc) | Body (doc) | Code hiện tại |
|------------|--------------|------------|----------------|
| IDLE | --- STANDBY --- | Waiting for Order... | ✅ "--- STANDBY ---" / "Waiting for" "Order..." |
| ST_OUTBOUND | DELIVERING... | To: [Patient] Next: [RFID_ID] | ✅ "DELIVERING..." / "To:" / "->" nextNode |
| OBSTACLE | !! WARNING !! | Object Detected! Braking... | ✅ "!! WARNING !!" / "Object Detected!" / "Braking..." |
| WAIT AT DEST | ARRIVED! | Please take items / Press SW to Return | ✅ "ARRIVED!" / "Please take items" / "Press SW to Return" |
| FOLLOW MODE | FOLLOWING... | Target: [Face/Body] Dist: [ToF] cm | ✅ "FOLLOWING..." / "Target: Locked" / "Dist: X cm" |
| RECOVERY | RECOVERING... | Searching Line... Step: [1/2/3] | ✅ "RECOVERING..." / "Searching Line..." (step 1) / "Step: 1/3" |

---

## Tóm tắt – đã patch

1. **Chế độ 1 – Servo Y:** ✅ Đặt Servo Y nghỉ (SERVO_Y_LEVEL) khi vào OUTBOUND/BACK/MISSION_DELEGATED.
2. **Chế độ 1 – Xuất phát:** ✅ Chỉ start khi đã quét MED + bấm SW 1 lần (missionDelegateSendRoutesOnly trên assign; missionDelegateStartMission khi SW+MED).
3. **Chế độ 1 – Tới đích:** ✅ Slave xoay 180° tại đích (SL_AT_DEST_UTURN), delay/line check (LINE_REACQUIRE_MS hoặc lineDetected()) rồi WAIT_AT_DEST.
4. **Chế độ 1 – Trở về:** ✅ Slave chờ startReturn từ Master (user bấm SW tại WAIT_AT_DEST); không tự BACK sau 2s.
5. **Chế độ 1 – Kết thúc:** ✅ Slave xoay 180° tại MED (SL_AT_MED_UTURN) rồi report BACK; Master routeClear, IDLE.
6. **Chế độ 2 & 3:** Bấm đúp SW (double-click) chưa thêm; vẫn dùng long press cho Follow/Recovery.
7. **OLED:** ✅ "Press SW to Return", "Braking...".
8. **Dò line:** Giữ 3 mắt (doc xác nhận).
