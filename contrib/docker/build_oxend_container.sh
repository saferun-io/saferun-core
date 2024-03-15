RELEASE=9.1.0
docker build -t oxend:${RELEASE} -f Dockerfile.oxend --build-arg USER_ID=$(id -u) --build-arg GROUP_ID=$(id -g) .
