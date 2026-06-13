#!/bin/bash
# 一键清理部署脚本
# 用法：在本地 Git Bash 执行

FIND_JOB="/c/Users/zhuxiangbo/Desktop/project/find_job"
SERVER="root@8.140.232.52"
REMOTE="/opt/find_job"

echo "=== 清理服务器上的 .pyc 缓存 ==="
ssh $SERVER "find $REMOTE -name '*.pyc' -delete && find $REMOTE -name '__pycache__' -type d -exec rm -rf {} + 2>/dev/null"

echo "=== 上传所有源文件 ==="
scp $FIND_JOB/job_scraper/main.py          $SERVER:$REMOTE/job_scraper/
scp $FIND_JOB/job_scraper/scrapers/boss.py $SERVER:$REMOTE/job_scraper/scrapers/
scp $FIND_JOB/job_scraper/auto_applier.py  $SERVER:$REMOTE/job_scraper/
scp $FIND_JOB/job_scraper/scrapers/base.py $SERVER:$REMOTE/job_scraper/scrapers/
scp $FIND_JOB/config.yaml                  $SERVER:$REMOTE/

echo "=== 修复所有 tab 为空格 ==="
ssh $SERVER "python3 -c \"
for path in ['$REMOTE/job_scraper/main.py', '$REMOTE/job_scraper/scrapers/boss.py', '$REMOTE/job_scraper/auto_applier.py', '$REMOTE/job_scraper/scrapers/base.py']:
    with open(path, 'r') as f: c = f.read()
    if '\\t' in c:
        c = c.replace('\\t', '    ')
        with open(path, 'w') as f: f.write(c)
        print('fixed tabs:', path)
    else:
        print('clean:', path)
\""

echo "=== 验证语法 ==="
ssh $SERVER "python3 -c \"compile(open('$REMOTE/job_scraper/main.py').read(), 'main.py', 'exec'); print('main.py OK')\""
ssh $SERVER "python3 -c \"compile(open('$REMOTE/job_scraper/scrapers/boss.py').read(), 'boss.py', 'exec'); print('boss.py OK')\""

echo "=== 语法全部通过，准备运行 ==="
echo "执行以下命令启动："
echo "ssh $SERVER"
echo "cd $REMOTE"
echo "pgrep Xvfb || Xvfb :99 -screen 0 1024x768x24 &"
echo "DISPLAY=:99 python3 -m job_scraper.main"
