How to set up and build:

# For preliminary testing on a tmpfs:

## OpenLDAP

### Build
```
# Read INSTALL

sudo apt install libdb5.3-dev libdb5.3++-dev
./configure
make depend
make
```

### Config

- libraries/libldap/ldap.conf
- servers/slapd/slapd.conf


### Run


## LB

### Build

```
sudo apt install golang
export GOPATH=$(realpath ./go)
export PATH=$GOPATH/bin:$PATH
go get github.com/hamano/lb
```

