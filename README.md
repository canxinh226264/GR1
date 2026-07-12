# MARL MERA-MST Wireless Sensor Network - OMNeT++ 6.3

Project này tái hiện ở mức gói tin hệ thống trong bài *Energy-Efficient Routing Algorithm for Wireless Sensor Networks: A Multi-Agent Reinforcement Learning Approach* (arXiv:2508.14679v1).

## Nội dung đã mô hình hóa

- WSN đơn cụm, 100 nút, mỗi nút bắt đầu với SoC 100%.
- Nút transmitter được chọn động theo SoC còn lại lớn nhất.
- Nguồn dữ liệu xuất hiện ở các vị trí ngẫu nhiên theo từng episode.
- Định tuyến đa chặng bị giới hạn bởi bán kính truyền, số hop và signal integrity tối thiểu 70%; suy hao tối đa mặc định là 4% ở một hop dài bằng communication range (bài không công bố giá trị N cụ thể). Nếu tuyến học tăng quá nhiều hop, hệ thống dùng tuyến vật lý ngắn nhất còn thỏa ràng buộc làm phương án an toàn.
- Mỗi nút có Q-table riêng. State rời rạc gồm SoC, khoảng cách đến sink/transmitter, hàng đợi, hop ước lượng, năng lượng lân cận và vai trò.
- Action là chọn next hop. Q-learning dùng alpha, gamma và epsilon-greedy.
- MERA dùng trọng số nghịch đảo SoC; MST tạo xương sống; graph ứng viên là MST cộng tối đa 6 cạnh lân cận giàu năng lượng mỗi nút.
- Reward dùng các giá trị trong Fig. 3: sensing +3, forwarding +5, sink +10, tránh hotspot +2, overuse -4, bảo toàn nút +3, giảm variance +2, duy trì mean SoC +2.
- Chi phí năng lượng trong Table I: multi-hop -2, transmitter-to-sink -8.
- Trễ gồm decision, queue, processing, transmission và propagation. CloudMARL thêm 15 ms cho uplink/computation/downlink.
- Baseline có SPMH và LEACH rút gọn để so sánh cùng một topology/traffic; LEACH tính thêm 40 ms setup/re-clustering mỗi round.

Đây là mô hình thuật toán ở mức packet/event, không phải mô phỏng PHY chi tiết như 802.15.4. Vì vậy không cần INET và project chạy độc lập trên OMNeT++ 6.3.

## Biên dịch

Project sau khi tại về phải được đặt tại `samples/marl_wsn` trong thư mục OMNeT++.

Trong IDE: chọn **File > Import > General > Existing Projects into Workspace**, trỏ tới `samples/marl_wsn`, sau đó **Build Project** và mở `omnetpp.ini` để bấm **Run**. Metadata `.project`, `.cproject` và `.oppbuildspec` đã có sẵn.

## Các cấu hình

| Config | Ý nghĩa |
|---|---|
| `MARL` | Phương pháp đề xuất: local Q-learning + MERA-MST |
| `CloudMARL` | MARL trên cloud, tính cả decision delay |
| `SPMH` | Baseline shortest-path multi-hop |
| `LEACH` | Baseline LEACH rút gọn |
| `Paper3D` | 100 nút trong khối 10x10x10 theo Table II |
| `TableI50Strict` | Đúng literal Table I: 50 nút, 100x100, r=9 |

Các vector chính: `meanSoC`, `SoCVariance`, `aliveNodes`, `deliveryRatio`, `meanEndToEndDelay`, `meanHopCount`, `totalReward`, `epsilon`.

## Điểm không nhất quán trong bài báo

Bài báo đưa ba bộ mô tả khác nhau: Table I ghi 50 nút trong 100x100 với r=9; phần Local Computing và các hình dùng 100 nút; Table II lại dùng 100 nút trong 10x10x10. Default `MARL` dùng 100 nút 2D và range 18 để topology jittered-grid liên thông, phù hợp các hình 6-11. `TableI50Strict` giữ nguyên r=9 nhưng với bố trí ngẫu nhiên có thể tạo graph rời rạc; hiện tượng drop trong config này là hệ quả của chính bộ tham số đó. Mọi tham số đều chỉnh trực tiếp được trong `omnetpp.ini`.

## Đọc kết quả

Trong IDE, mở Result Analysis và nạp thư mục `results`. Có thể xuất CSV trong OMNeT++ Shell, ví dụ:

```sh
opp_scavetool x results/MARL-0.vec -o results/MARL-vectors.csv -F CSV-R
opp_scavetool x results/MARL-0.sca -o results/MARL-scalars.csv -F CSV-R
```
