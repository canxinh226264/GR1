WSN MARL + MERA-MST Visual Simulator
====================================

Cach chay:
  1. Mo file index.html bang Chrome/Edge.
  2. Bam Reset de tao mang moi.
  3. Bam Next round de quan sat tung round.
  4. Bam Play/Pause neu muon chay tu dong.
  5. Re chuot vao node de xem ID, trang thai, SoC, queue, degree, route va toa do 3D.
  6. Nhan giu chuot trai tren topology roi keo de xoay mang 3D.
  7. Bam Prev round de xem lai snapshot round truoc do.

Chuong trinh mo phong:
  - tao node WSN ngau nhien;
  - gan toa do 3D (x, y, z) cho tung node;
  - tao graph ket noi theo communication range;
  - chon transmitter theo SoC, vi tri va queue;
  - tinh MERA weight dua tren SoC;
  - tinh MST bang Kruskal;
  - tao fused graph bang lambda;
  - chon route bang Dijkstra tren fused MERA-MST cost co anh huong Q-learning;
  - cap nhat queue, SoC, reward, delay;
  - ve route cua round hien tai va metric theo thoi gian.
  - hien thi tooltip node va cho phep xoay topology 3D bang chuot.
  - moi lan Reset tu sinh seed moi de node phan bo ngau nhien khac nhau.
  - luu snapshot tung round de xem lai trang thai route/node cua round truoc.

Y nghia tham so:
  lambda:
    Uu tien MERA. Lambda cao hon se tranh node pin yeu manh hon.

  gamma:
    Discount factor cua Q-learning. Gamma cao hon nghia la coi trong reward tuong lai hon.

  communication range:
    Tam truyen truc tiep giua cac node. Range cao hon thi graph day hon.

Ghi chu:
  Day la mo phong giao duc de quan sat co che tung round.
  No khong phai mo phong tang vat ly/mang chi tiet nhu ns-3 hay OMNeT++.
