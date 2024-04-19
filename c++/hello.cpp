#include<cstdio>
#include<iostream>
#include"print.h"

class log
{
public:
    static int a;

    int b;
    enum l
    {
        one,
        two,
        three
    };

    log(int b)
    {
        b = b;
    }

    static void waring()
    {
        ;
        std::cout << "warning!";
        std::cout << two <<std::endl;
    }

    void print()
    {
        std::cout << b << std::endl;
    }
};

int log::a = 1;
int main()
{
    log log(1);
    log::waring();
    log.print();
    std::cout<<log::l::two<<std::endl;
    print();
}