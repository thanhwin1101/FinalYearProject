Đóng vai là Senior Embedded Software Engineer. Hãy thực hiện kiểm tra mã nguồn (Code Audit) cho hai file src/auto_mode.cpp (ESP32) và src/auto_runner.cpp (STM32) dựa trên Tài liệu Đặc tả Luồng hoạt động (Flow Specification) dưới đây.

FLOW SPECIFICATION CẦN KIỂM TRA:

Nhận Route: ESP32 nhận outboundRoute và returnRoute từ MQTT, lưu trữ và gửi outboundRoute xuống STM32. STM32 chờ lệnh START từ nút bấm.

Chạy Outbound: Bấm nút, STM32 chạy theo route. Khi quét trúng Checkpoint: Bắt buộc phanh lại (brake) -> Thực hiện hành động (quay trái/phải/đi thẳng) -> Đi tiếp.

Đích đến (Destination): Quét Checkpoint cuối của Outbound -> Còi kêu, OLED hiện "ARRIVED" tại RO hoặc RM -> STM32 tự động quay 180 độ tại chỗ để chuẩn bị sẵn sàng đầu xe.

Chạy Return: Đợi user bấm nút xác nhận -> ESP32 gửi returnRoute xuống STM32 -> STM32 chạy lần lượt về MED.

Hoàn thành (Mission Done): Về đến MED (Checkpoint cuối của Return) -> STM32 tự động quay 180 độ để sẵn sàng cho mission tiếp theo.

Xử lý ToF: Gặp vật cản -> Phanh tạm thời. Hết vật cản -> Tự động đi tiếp.

Xử lý Mismatch / Cancel: Nếu Cancel, ESP32 báo vị trí mới lên Web để xin route về. Nếu quét sai Checkpoint (Mismatch), STM32 phải quay 180 độ ngay tại đó, sau đó ESP32 gửi yêu cầu xin lại Route về MED từ Web.

NHIỆM VỤ CỦA BẠN:

Phân tích đối chiếu: Đọc code hiện tại và chỉ ra những điểm CHƯA ĐÁP ỨNG ĐÚNG đặc tả trên (Ví dụ: Việc tự động quay 180 độ ở đích đến và quay 180 độ ở trạm MED đã được implement chưa? Logic Mismatch đã có lệnh quay 180 độ chưa?).

Viết lại mã nguồn: Đưa ra mã nguồn khắc phục cho các file auto_mode.cpp và auto_runner.cpp để flow chạy chính xác 100% như trên. Đảm bảo tuân thủ nguyên tắc Non-blocking (không dùng delay()).