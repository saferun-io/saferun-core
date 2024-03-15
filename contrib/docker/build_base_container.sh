RELEASE=20.04
curl -so oxen-deb-key.gpg https://deb.oxen.io/pub.gpg
docker build -t oxen-ubuntu:${RELEASE} -f Dockerfile.oxen-ubuntu .
rm oxen-deb-key.gpg
