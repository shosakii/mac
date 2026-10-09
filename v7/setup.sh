#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ns3_dir="$(cd "${project_dir}/.." && pwd)"
scratch_entry="${ns3_dir}/scratch/self-organizing-mac-v7"
adhoc_header="${ns3_dir}/src/wifi/model/adhoc-wifi-mac.h"
adhoc_source="${ns3_dir}/src/wifi/model/adhoc-wifi-mac.cc"
mac_header="${ns3_dir}/src/wifi/model/wifi-mac-header.h"
mac_source="${ns3_dir}/src/wifi/model/wifi-mac-header.cc"
qos_fem_header="${ns3_dir}/src/wifi/model/qos-frame-exchange-manager.h"
qos_fem_source="${ns3_dir}/src/wifi/model/qos-frame-exchange-manager.cc"
ht_fem_header="${ns3_dir}/src/wifi/model/ht/ht-frame-exchange-manager.h"
ht_fem_source="${ns3_dir}/src/wifi/model/ht/ht-frame-exchange-manager.cc"

if [[ ! -x "${ns3_dir}/ns3" || ! -f "${ns3_dir}/CMakeLists.txt" ]]; then
    echo "错误：请把 self-organizing-mac 放在 ns-3.47 根目录下。" >&2
    exit 1
fi

backup_dir="${project_dir}/backups/before-v7-setup"
mkdir -p "${backup_dir}"
for file in "${adhoc_header}" "${adhoc_source}" "${mac_header}" "${mac_source}" \
            "${qos_fem_header}" "${qos_fem_source}" "${ht_fem_header}" "${ht_fem_source}"; do
    name="$(basename "${file}")"
    [[ -f "${backup_dir}/${name}" ]] || cp "${file}" "${backup_dir}/${name}"
done
cp "${project_dir}/patches/adhoc-wifi-mac.h" "${adhoc_header}"
cp "${project_dir}/patches/adhoc-wifi-mac.cc" "${adhoc_source}"
cp "${project_dir}/patches/wifi-mac-header.h" "${mac_header}"
cp "${project_dir}/patches/wifi-mac-header.cc" "${mac_source}"
cp "${project_dir}/patches/qos-frame-exchange-manager.h" "${qos_fem_header}"
cp "${project_dir}/patches/qos-frame-exchange-manager.cc" "${qos_fem_source}"
cp "${project_dir}/patches/ht-frame-exchange-manager.h" "${ht_fem_header}"
cp "${project_dir}/patches/ht-frame-exchange-manager.cc" "${ht_fem_source}"
echo "已安装V7 Beacon/QoS Data短地址编解码、QoS数据窗门控与逐节点生命史扩展。"

if [[ -L "${scratch_entry}" ]]; then
    current_target="$(readlink -f "${scratch_entry}")"
    if [[ "${current_target}" != "${project_dir}" ]]; then
        echo "错误：${scratch_entry} 已指向其他目录：${current_target}" >&2
        exit 1
    fi
elif [[ -e "${scratch_entry}" ]]; then
    echo "错误：${scratch_entry} 已存在且不是符号链接，请人工检查。" >&2
    exit 1
else
    ln -s ../self-organizing-mac-v7 "${scratch_entry}"
fi

cd "${ns3_dir}"
./ns3 configure --enable-examples
./ns3 build self-organizing-64node-v7

echo "安装完成。示例："
echo "  ./self-organizing-mac-v7/run.sh --duration=35 --controlFrame=beacon --beaconAddress=standard --dataAddress=short --controlContenders=8 --neighborEncoding=adaptive --density=sparse --dataQueuePolicy=defer --amsduBytes=0 --macQueueAdmissionLimit=4 --output=self-organizing-mac-v7/results/example"
