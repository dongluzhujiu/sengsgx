# SENG, the SGX-Enforcing Network Gateway

Documentation will be added in the near future.

* seng_runtime/ --  contains the client-side component based on Graphene-SGX

* seng_server/  --  contains the server-side component based on libuv

* seng_sdk/ --  contains the alternative client-side component without LibOS, but rather based on the Intel SGX SDK

Note: The following build steps were tested under Ubuntu 16.04.6 LTS and kernel 4.15.0-91. The client-side has already been successfully tested with an older container version on Ubuntu 18.04.2 LTS and kernel 4.15-0-47.

#Preparation
1. pull submodules:
	`git submodule update --init --recursive`

2. patch sgx-ra-tls:
	$cd sgx-ra-tls/
	$patch -p1 < ../patches/sgx-ra-tls_seng_changes.patch
3. add your own Intel Developer SPID to ra_tls_options.c:
	$vim sgx-ra-tls/ra_tls_options.c
4. add quote_type in ra_tls_options.c:
	$vim sgx-ra-tls/ra_tls_options.c
	Options:
	(a) SGX_LINKABLE_SIGNATURE
	(b) SGX_UNLINKABLE_SIGNATURE
5. add your own Intel Developer remote attestation subscription key to ias-ra.c:
	$vim sgx-ra-tls/ias-ra.c
	at line 228, replace the "YYY" with your subscription key

6. replace UID for container user, e.g., with `id --user` (cf. %TODO in dockerfile):
	$vim base_container/Dockerfile
7. build base container:
	$cd base_container/
	$docker-compose build
	Note: will try to install headers of your host kernel version
8. build sgx-ra-tls:
	$cd base_container/
	$docker-compose run --user encl-dev base-container
	$cd ~/sgx-ra-tls/
	$./build.sh graphene  *TODO* fix error/disable wolfssl build
	$./build.sh graphene
	$make

9. generate RSA key pair for server.
	e.g., for a demo:
	$cd seng_server/
	$openssl req -x509 -newkey rsa:3072 -keyout srv_key.pem -out srv_cert.pem -days 365 -nodes

#SENG Server
1. build server container:
	$cd seng_server/
	$docker-compose build
2. build SENG server:
	$cd seng_server/
	$docker-compose run --user encl-dev seng-server
	$cd ~/seng_server/double_tunnel_openssl/
	$mkdir build
	$cd build/
	$cmake .. -DSGX_MODE=HW -DCMAKE_BUILD_TYPE=RELEASE
	$make

3. symlink server key pair:
	$cd build/
	$ln -s ../../srv_key.pem .
	$ln -s ../../srv_cert.pem .
4. configure SENG network interface:
 	Options:
 	(a) from host system (if running "host" networking mode in server container)
		$cd seng_server
		$./setup_seng_interface.bash
	(b) from inside server container [*TODO* requires installing iproute2 inside container]
		$sudo ~/seng_server/setup_seng_interface.bash
	
	note: if running "host" networking mode, (b) might run into nftables-iptables issues;

	*TODO* TUN device subnetwork must not collide with existing ones of the server;
	*TODO* alternatively might consider removing "host" networking mode and add port binding/forwarding to container
5. configure forwarding rules:
	*TODO* iptables rules to allow forwarding traffic between TUN interface and in/out host network interface(s)

6. Optional: Generate Sqlite3 demo database:
	$cd ~/seng_server/double_tunnel_openssl/
	$sqlite3 demo_sqlite3.db < seng_db_creator.sql

7. run SENG server:
	$cd build/
	$sudo ./src/seng_ossl_double_tunnel_server [-d <db>] <port>
8. gracefully shutdown SENG server:
	ctrl+C, wait

#SENG Client-side

##Prerequisites
1. Install SGX driver at host
	we tested: https://github.com/intel/linux-sgx-driver/archive/2a509c203533f9950fa3459fe91864051bc021a2.zip
	$sudo apt-get install linux-headers-$(uname -r)
	$mkdir external/
	$cd external/
	$wget https://github.com/intel/linux-sgx-driver/archive/2a509c203533f9950fa3459fe91864051bc021a2.zip
	$unzip \*.zip

	build and install (cf. shipped instructions for installation); for demo, e.g.:
	$make
	$sudo insmod isgx.ko

	*note*: cf. driver readme for proper installation

2. Install PSW version 2.7.1 at host
	e.g. for Ubuntu 16.04 LTS:
	$cd external/

	$wget https://download.01.org/intel-sgx/sgx-linux/2.7.1/distro/ubuntu16.04-server/libsgx-enclave-common_2.7.101.3-xenial1_amd64.deb
	$sudo dpkg -i ./libsgx-enclave-common_2.7.101.3-xenial1_amd64.deb
	
	$wget https://download.01.org/intel-sgx/sgx-linux/2.7.1/distro/ubuntu16.04-server/libsgx-enclave-common-dev_2.7.101.3-xenial1_amd64.deb
	$sudo dpkg -i ./libsgx-enclave-common-dev_2.7.101.3-xenial1_amd64.deb
	
	check that aesmd is running:
	$service aesmd status
	"/var/run/aesmd/aesm.socket" should exist now

	*NOTE* "SealedData" SDK sample did not work for us under that setup; (it worked under different SDK/PSW versions though, so we guess it is fixed under different versions/platforms)

3. Install Graphene-SGX driver (legacy?) at host (optionally from build container), e.g.:
	$sudo insmod sgx-ra-tls/deps/graphene/Pal/src/host/Linux-SGX/sgx-driver/graphene-sgx.ko

##SENG Runtime
1. patch lwIP:
	$cd seng_runtime/lwip_based_client_lib/externals/lwip/
	$patch -p1 < ../../../../patches/total_lwip_patcher.patch

2. build runtime container:
	$cd seng_runtime/
	$docker-compose build

3. build SENG server:
	$cd seng_runtime/
	$docker-compose run --user encl-dev seng-runtime
	$cd ~/client_enclave/lwip_based_client_lib/
	$mkdir build
	$cd build/
	$cmake .. -DSGX_MODE=HW -DCMAKE_BUILD_TYPE=RELEASE
	$make

4. *TODO*: demo/bench app(s) with instructions for Graphene symlinks, manifest, etc.


##SENG SDK
1. fetch libraries:
	$cd seng_sdk/
	$./fetch_external_libs.bash
2. fetch NGINX and patch it:
	$./fetch_external_apps.bash

3. build SDK container:
	$docker-compose build

4. download, patch and build Intel SGX SDK+PSW:
	$docker-compose run --user encl-dev seng-sdk
	$cd ~/seng_sdk/
	$./build_patched_sdkpsw_sgxssl.bash

5. generate enclave signing key pair with exponent 3 for demo app and Nginx:
	$cd seng_sdk/enclave/app/src/
	$openssl genrsa -out app_enclave_private.pem -3 3072
	$openssl rsa -in app_enclave_private.pem -pubout -out app_enclave_public.pem

6. add public key of SENG server:
	$cd seng_sdk/
	$vim enclave/seng/src/DT_SSLEngineClient_OpenSSL.cpp

	at line 52, replace "ADD_YOURS" in the "ngw_hc_cert" variable with your public server key without newlines as should be output by:
	$sed '1d;$d;' ../seng_server/srv_cert.pem|tr -d \\n|tr \& \\\&

7. build SENG SDK libraries with demo app, followed by SENG-NGINX:
	$docker-compose run --user encl-dev seng-sdk
		[press "y" or "Y" on SDK/PSW re-install prompt]
	$cd ~/seng_sdk/
	$mkdir build
	$cd build/
	$cmake .. -DSGX_HW=ON -DCMAKE_BUILD_TYPE=RELEASE
	$make
	-----
	$cd ..
	$cd ported_external_apps/nginx-1.10.3/
	$../build_seng_nginx.bash



#TODOs
##sgx-ra-tls
* simplfy configuration of subscription key (note: newer version of sgx-ra-tls now also support the new IAS authentication method)
* option to only build the minimum of sgx-ra-tls required for the SENG server
* fix wolfssl compilation issue / disable unrequired builds in sgx-ra-tls

##server
* make the IP address configuration/bindings in SENG server easier configurable + more dynamic

##sdk
* FIX: currently SDK and PSW are installed inside the container, i.e., on restarts/reinstantiation, both are gone again; temporary work-arounded by adding the option to cause a direct re-install on container session start if it has been compiled before