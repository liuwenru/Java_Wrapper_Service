# Java_Wrapper_Service
JavaWrapperService的代码阅读笔记.添加了自定义日志内容触发执行脚本的功能




```bash

wrapper.filter.trigger.1000=日志触发关键字
wrapper.filter.action.1000=CUSTOMTASK  


```

当触发时会执行`wrapper.conf`所在目录下的`task.sh`文件
