# Explorer - 解析器

# Install Guide
## ubuntu 12.04 / 14.04

### oh my zsh
```
apt-get install zsh git
wget --no-check-certificate http://install.ohmyz.sh -O - | sh
```

### 环境
```
apt-get install -y cmake build-essential autotools-dev libtool autoconf automake
```

### 依赖
```
apt-get install -y libboost-dev libboost-thread-dev libboost-system-dev libboost-regex-dev libboost-filesystem-dev openssl libssl-dev libmysqlclient-dev libcurl4-openssl-dev
```

# Test

```
cd build

# run all test case
./unittest

# run one test case
./unittest --gtest_filter=Common\*
```
