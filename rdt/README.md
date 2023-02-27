# Lab1-Reliable Data Transport Protocol

## 信息

学号：520021910550
姓名：杨景凯
邮件：sxqxyjk2020@sjtu.edu.cn

## 实现细节

### 包头设计

使用8字节包头，包含4字节的checksum，1字节的payload_size，1字节的index，1字节的sequence_number，1字节的acknowledgment_sequence_number。

- checksum：
  - 我使用了CRC去实现checksum。这是因为CRC具有更好的识别错误能力。而由于普通的checksum求和的期望与原值相等，故错误可能被更大概率遗漏。
  - 我使用32位checksum，经过测试，在1000次测试，每次模拟时间1000s，传输耗时0.1s，平均大小100bytes，乱序概率0.3，丢失概率0.3，错误概率0.3，最终错误次数为0次。
- payload_size：即存在的data大小。
- index：指在当前message中的位置。
- sequence_number：指传输序号。
  - 传输序号是一个小于10\*WINDOW_SIZE的数值，使得其可以在1个字节内不溢出。当序号增长到大于10\*WINDOW_SIZE时，即变为0。这样设计是为了receiver能够知道当前的序号，将过时的包进行丢弃，同时根据此值更新window。
- acknowledgment_sequence_number：确认号。简单地将传输序号复制到此位置，表示当前序号已经被receiver确认。

## 发送设计

- 维护一个链表，存储所有未发送的包，这样使得上层的消息不会被阻断。当消息到来时，将其转换为包，并加入链表的尾部。
- 当收到包时，更新window中当前位置包的接收状态，并检查是否能够更新整个window。
- 当当前window中所有包已经被确认收到，即更新window，将当前window中包delete（防止内存泄漏）,从链表中取出包，放入window。由于只有一个全局的timer，因此我们不能针对每个包设置timer，只能对整个窗口设置。
- 当超时时，检查window中未被确认收到的包，重新发送。

## 接收设计

- 当收到包时，根据包头中的sequence_number更新window。如果发送端已经是下一组window，则清理所有window中的包，按顺序发送消息。如果是当前组，则简单赋值。如果是过时组，则丢弃。
  - 正确性保证：当发送端更新时，接收端所有应该接收到的包已经被全部接收到，因此可以更新。
  - 适应性保证：接收端并不知道当前window总共需要发送多少个包，因此这样设计使得接收端不被阻塞。这样也保证了唯一性，重复发送的包只被向上层发送一次。
- 最终，将window中所有包发送到上层。