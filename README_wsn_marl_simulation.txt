WSN MARL + MERA-MST Simulation
==============================

File chính:
  outputs/wsn_marl_mera_mst_sim.py

Chạy nhanh:
  python outputs\wsn_marl_mera_mst_sim.py

Chạy với cấu hình gần bài báo hơn:
  python outputs\wsn_marl_mera_mst_sim.py --nodes 100 --episodes 100 --lambda-priority 0.85 --output outputs\wsn_simulation_results_tuned

Kết quả mặc định sẽ nằm ở:
  outputs/wsn_simulation_results/

Các file sinh ra:
  metrics.csv
    Bảng số liệu theo từng episode cho proposed và baseline.

  summary.json
    Tóm tắt cấu hình và kết quả cuối.

  active_nodes.svg
    Số node còn sống theo episode.

  avg_soc.svg
    SoC trung bình theo episode.

  soc_variance.svg
    Phương sai SoC theo episode.

  reward.svg
    Tổng reward theo episode.

  delay.svg
    Delay trung bình theo episode.

  topology_initial.svg
    Topology ban đầu.

  topology_final_proposed.svg
    Topology cuối của phương pháp MARL + MERA-MST.

  topology_final_baseline.svg
    Topology cuối của baseline shortest-path.

Ý nghĩa một số tham số:
  --lambda-priority
    Chính là lambda trong MERA-MST fusion.
    Giá trị cao hơn ưu tiên năng lượng nhiều hơn.
    Ví dụ: 0.85 nghĩa là ưu tiên tránh node pin yếu mạnh hơn.

  --gamma
    Discount factor của Q-learning.
    Giá trị cao hơn nghĩa là agent coi trọng reward tương lai hơn.

  --alpha
    Learning rate của Q-learning.

  --cloud-delay
    Thêm độ trễ kiểu cloud-assisted vào mô hình delay.

Ghi chú:
  Đây là bản giả lập giáo dục, dùng pure Python để dễ chạy.
  Nó mô phỏng ý tưởng chính của bài báo, không phải mô phỏng PHY/MAC-level như ns-3 hay OMNeT++.
  Các SVG có thể mở bằng trình duyệt hoặc chèn vào PowerPoint.
