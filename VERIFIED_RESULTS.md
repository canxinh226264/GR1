# Kết quả kiểm thử OMNeT++ 6.3

Ngày kiểm thử: 2026-07-10. Seed: 42. Mỗi cấu hình chạy 100 episode; default có 100 nút và 4 nguồn dữ liệu mỗi episode.

| Config | Mean SoC cuối (%) | SoC variance cuối | Nút còn sống | Delivery (%) | Delay trung bình (ms) | Khoảng delay (ms) | Hop trung bình |
|---|---:|---:|---:|---:|---:|---:|---:|
| MARL | 27.30 | 57.24 | 100 | 100.00 | 46.93 | 30.2-61.9 | 5.92 |
| CloudMARL | 26.82 | 57.03 | 100 | 100.00 | 61.61 | 49.2-73.3 | 6.10 |
| SPMH | 36.52 | 1453.55 | 57 | 73.75 | 43.73 | 16.3-65.2 | 5.73 |
| LEACH | 55.87 | 456.88 | 99 | 99.75 | 67.60 | 61.9-75.4 | 2.47 |
| Paper3D | 40.77 | 73.09 | 100 | 100.00 | 38.28 | 28.8-50.4 | 4.27 |

Kết quả cho thấy mục tiêu chính được tái hiện: MARL phân bố tiêu thụ năng lượng đều hơn và giữ toàn bộ nút hoạt động; SPMH giữ mean SoC cao hơn vì nhiều gói bị drop, nhưng làm cạn các relay trung tâm và mất 43 nút. LEACH ở đây là baseline rút gọn; setup/re-clustering delay 40 ms được tính nhưng TDMA/PHY 802.15.4 không được mô hình hóa chi tiết.

Đây là một lần chạy xác minh chức năng, không phải khoảng tin cậy thống kê. Khi đánh giá học thuật, nên đặt `repeat = 10` hoặc cao hơn và dùng `seed-set = ${repetition}` trong `omnetpp.ini`.
