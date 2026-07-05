#include <iostream>
#include <vector>

class iPrefetchHistory {
public:
    iPrefetchHistory(int size) : size_(size), head_(0), tail_(0) {
        data_ = new int[size_];
    }

    ~iPrefetchHistory() {
        delete[] data_;
    }

    void push(int value) {
        data_[tail_] = value;
        tail_ = (tail_ + 1) % size_;

        // Overwrite the oldest value if the buffer is full
        if (tail_ == head_) {
            head_ = (head_ + 1) % size_;
        }
    }

    int pop() {
        if (head_ == tail_) {
            throw std::runtime_error("Buffer is empty");
        }
        int result = data_[head_];
        head_ = (head_ + 1) % size_;
        return result;
    }

    bool isFull() {
        return (tail_ + 1) % size_ == head_;
    }

    void display() {
        std::cout << "Buffer: [";
        for (int i = head_; i != tail_; i = (i + 1) % size_) {
            std::cout << data_[i] << ", ";
        }
        std::cout << "]" << std::endl;
    }

    void remove(int value) {
        int cur = head_;
        int prev = -1;
        while (cur != tail_) {
            if (data_[cur] == value) {
                if (prev == -1) {
                    head_ = (head_ + 1) % size_;
                }
                else {
                    for (int i = cur; i != tail_ - 1; i = (i + 1) % size_) {
                        data_[i] = data_[(i + 1) % size_];
                    }
                    tail_ = (tail_ + size_ - 1) % size_;
                }
                return;
            }
            prev = cur;
            cur = (cur + 1) % size_;
        }
    }

private:
    int* data_;
    int size_;
    int head_;
    int tail_;
};
