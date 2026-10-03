# Shared by the native build and host integration tests; sourced, not executed.
ssh_flags=(-DWOLFSSL_USER_SETTINGS -Iapps/ssh -Iapps/vendor/wolfssh -Iapps/vendor/wolfssl -Iapps/vendor/libvterm/include)
ssh_crypto=()
for ssh_source in aes asn coding curve25519 ecc ed25519 error fe_operations ge_operations fe_low_mem ge_low_mem signature hash hmac kdf logging memory random rsa sha256 sha512 sp_int wc_port wolfmath; do
 ssh_crypto+=("apps/vendor/wolfssl/wolfcrypt/src/$ssh_source.c")
done
ssh_sources=(apps/vendor/wolfssh/src/{ssh,internal,log,io,port}.c apps/vendor/bcrypt/bcrypt_pbkdf.c "${ssh_crypto[@]}")

ssh_sources+=(apps/vendor/libvterm/src/*.c)
ssh_sources+=(apps/ssh/{client,util,seed,keys,known_hosts,terminal}.c)
