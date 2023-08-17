#!/usr/bin/env bash


# 27.01.2023 elise.jennings@par-tec.com
# Sets the Lmod module path for packages installed in /homes/software. 
#
# Admins adding packages (on login node) need to update $SPACK_HOME//etc/spack/defaults/modules.yaml if needed 
# and fresh the modulefiles $> spack module lmod refresh  -y for any new packages to be visible.


BASE=/etc/modulefiles:/usr/share/modulefiles
MOD_PATH=$BASE:/parastation/software/modulefiles/linux-amzn2-aarch64/Core:/parastation/software/modulefiles/linux-amzn2-aarch64/gcc/11.3.0

cat <<EOF > /etc/profile.d/00-modulepath.csh
if (! \$?MODULEPATH && ( \`readlink /etc/alternatives/modules.csh\` == /usr/share/lmod/lmod/init/cshrc || -f /etc/profile.d/z00_lmod.csh ) ) then
  setenv MODULEPATH $MOD_PATH
endif
EOF

cat <<EOF > /etc/profile.d/00-modulepath.sh
[ -z "\$MODULEPATH" ] &&
  [ "\$(readlink /etc/alternatives/modules.sh)" = "/usr/share/lmod/lmod/init/profile" -o -f /etc/profile.d/z00_lmod.sh ] &&
  export MODULEPATH=$MOD_PATH || :
EOF
