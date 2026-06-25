#GR1
WSN MARL + MERA-MST Visual Simulator
====================================

Cách chạy:

Mở file index.html bằng Chrome/Edge.

Bấm Reset để tạo mạng mới.

Bấm Next round để quan sát từng round (vòng).

Bấm Play/Pause nếu muốn chạy tự động.

Rê chuột vào node (nút) để xem ID, trạng thái, SoC (dung lượng pin còn lại), queue (hàng đợi), degree (bậc của nút), route (tuyến đường) và tọa độ 3D.

Nhấn giữ chuột trái trên topology (sơ đồ mạng) rồi kéo để xoay mạng 3D.

Bấm Prev round để xem lại snapshot (ảnh chụp trạng thái) của round trước đó.

Chương trình mô phỏng:

Tạo các node WSN ngẫu nhiên;

Gắn tọa độ 3D (x, y, z) cho từng node;

Tạo graph (đồ thị) kết nối theo communication range (phạm vi truyền thông);

Chọn transmitter (bộ phát) theo SoC, vị trí và queue;

Tính MERA weight (trọng số MERA) dựa trên SoC;

Tính MST (cây khung tối tiểu) bằng thuật toán Kruskal;

Tạo fused graph (đồ thị hợp nhất) bằng tham số lambda;

Chọn route bằng thuật toán Dijkstra trên fused MERA-MST cost (chi phí MERA-MST hợp nhất) có ảnh hưởng của Q-learning;

Cập nhật queue, SoC, reward (phần thưởng) và delay (độ trễ);

Vẽ route của round hiện tại và các metric (chỉ số cơ bản) theo thời gian;

Hiển thị tooltip của node và cho phép xoay topology 3D bằng chuột;

Mỗi lần Reset tự sinh seed (hạt giống) mới để các node phân bố ngẫu nhiên khác nhau;

Lưu snapshot từng round để xem lại trạng thái route/node của round trước.

Ý nghĩa tham số:
lambda:
Ưu tiên MERA. Lambda cao hơn sẽ tránh các node pin yếu mạnh mẽ hơn.

gamma:
Discount factor (hệ số chiết khấu) của Q-learning. Gamma cao hơn nghĩa là coi trọng reward trong tương lai hơn.

communication range:
Tầm truyền trực tiếp giữa các node. Range cao hơn thì graph sẽ dày đặc hơn.

Ghi chú:

Đây là mô phỏng mang tính giáo dục để quan sát cơ chế hoạt động của từng round. Nó không phải là mô phỏng tầng vật lý/mạng chi tiết như ns-3 hay OMNeT++.
