# UDP2TCP
### 基于UDP服务设计可靠传输协议并编程实现
实验3-1：利用数据报套接字在用户空间实现面向连接的可靠数据传输，功能包括：建立连接、差错检测、确认重传等。流量控制采用停等机制，完成给定测试文件的传输。

实验3-2：在实验3-1的基础上，将停等机制改成基于滑动窗口的流量控制机制，采用固定窗口大小，支持累积确认，完成给定测试文件的传输。

实验3-3：在实验3-2的基础上，选择实现一种拥塞控制算法，也可以是改进的算法，完成给定测试文件的传输。

实验3-4：基于给定的实验测试环境，通过改变延迟时间和丢包率，完成下面3组性能对比实验：
- 停等机制与滑动窗口机制性能对比；
- 滑动窗口机制中不同窗口大小对性能的影响；
- 有拥塞控制和无拥塞控制的性能比较。
