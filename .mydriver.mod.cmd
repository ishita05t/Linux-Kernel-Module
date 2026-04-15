savedcmd_/workspaces/Linux-Kernel-Module/mydriver.mod := printf '%s\n'   mydriver.o | awk '!x[$$0]++ { print("/workspaces/Linux-Kernel-Module/"$$0) }' > /workspaces/Linux-Kernel-Module/mydriver.mod
