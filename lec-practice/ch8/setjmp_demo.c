#include <stdio.h>
#include <setjmp.h>

jmp_buf env;    // 声明一个 jmp_buf 类型的变量，用于保存环境信息

void level3(){
    printf("level3: find error,back to main\n");
    longjmp(env, 1);    // 回到 setjmp 的位置，返回值为 1
    printf("level3: this line will not be executed\n");
}

void level2(){
    printf("level2: call level3\n");
    level3();
    printf("level2: this line will not be executed\n");
}

void level1(){
    printf("level1: call level2\n");
    level2();
    printf("level1: this line will not be executed\n");
}

int main(){

    int rc = setjmp(env);    // 保存当前环境，返回值为 0
    if(rc == 0){
        printf("main: call level1\n");
        level1();
        printf("main: this line will not be executed\n");
    } else {
        printf("main: back from level3, rc = %d\n", rc);
    }
    printf("main: end\n");
    return 0;
}