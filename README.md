# KVM-cheat
这里有几个用来学习QEMU以及KVM的例程。
如果你是刚刚开始研究QEMu或者KVM，基本上可以从qemu-cheat.c这个文件开始读起来，里面有足够多的注释，如果还不够多，请来push request.
qemu-cheat.c实现了一个最最简单的QEMu以及其中包含了一个最最简单的虚拟机镜像，简单到镜像里面没有任何操作系统甚至说没有任何操作都更准确一点。
如果想继续了解镜像是如何生成的，可以参考img.S这个文件，以及Makefile中如何编译和链接成二进制的。

## 如何使用
在console里面执行：
```
git clone https://github.com/ysun/kvm-cheat.git 
```

这里默认读者会使用git，对于git的安装和使用请自行搜索。

```
cd kvm-cheat
```

编译项目
```
make
```

然后就可以执行这个最简单的qemu-cheat
```
./qemu-cheat
```

## 技术交流
对于本程序有任何问题，不妨访问博客[KVM 虚拟化原理2— QEMU启动过程](http://www.owalle.com/2018/12/10/kvm-boot/)留言交流

