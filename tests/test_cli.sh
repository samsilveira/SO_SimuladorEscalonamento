#!/bin/sh
set -eu

case "$1" in
    /*) simulator=$1 ;;
    *) simulator=$PWD/$1 ;;
esac
config_file=$PWD/configs/default.conf
temporary_directory=$(mktemp -d)
trap 'rm -rf -- "$temporary_directory"' EXIT HUP INT TERM
mkdir "$temporary_directory/sub"

fail() {
    echo "test_cli: $1" >&2
    exit 1
}

expect_failure() {
    if "$@" >/dev/null 2>&1; then
        fail "o comando deveria falhar: $*"
    fi
}

aggregate_header='schema_version,run_id,workload_sha256,algorithm,scenario,seed,process_count,context_switch_cost,rr_quantum,makespan,mean_turnaround,context_switches,jain_slowdown_pct,status'
individual_header='run_id,pid,arrival,completion,turnaround,ideal_time,slowdown,priority,total_cpu,total_io,io_requests'

"$simulator" \
    --config "$config_file" \
    --processes 2 \
    --run-id precedence \
    --output "$temporary_directory/result.csv" \
    --workload-output "$temporary_directory/workload.csv"

[ "$(sed -n '1p' "$temporary_directory/result.csv")" = "$aggregate_header" ] \
    || fail "schema agregado incorreto"
[ "$(awk -F, 'NR == 2 { print NF }' "$temporary_directory/result.csv")" = 14 ] \
    || fail "resultado agregado nao possui 14 campos"
[ "$(awk -F, 'NR == 2 { print $7 }' "$temporary_directory/result.csv")" = 2 ] \
    || fail "CLI nao sobrescreveu process_count da configuracao"
[ "$(awk -F, 'NR == 2 { print $14 }' "$temporary_directory/result.csv")" = success ] \
    || fail "status agregado incorreto"
[ ! -e "$temporary_directory/individual.csv" ] \
    || fail "metricas individuais foram criadas sem solicitacao"

printf '%s\n' \
    'schema_version=1' \
    'scenario=io_bound' \
    'algorithm=rr' \
    'seed=7' \
    'process_count=3' \
    'context_switch_cost=2' \
    'rr_quantum=5' \
    'arrival_min=0' \
    'arrival_max=3' > "$temporary_directory/valid.conf"
"$simulator" \
    --config "$temporary_directory/valid.conf" \
    --run-id config-loaded \
    --output "$temporary_directory/config-loaded.csv"
[ "$(awk -F, 'NR == 2 { print $4, $5, $6, $7, $8, $9 }' "$temporary_directory/config-loaded.csv")" \
    = "rr io_bound 7 3 2 5" ] || fail "arquivo de configuracao nao foi aplicado"
"$simulator" \
    --config "$temporary_directory/valid.conf" \
    --algorithm fcfs \
    --processes 2 \
    --run-id config-overridden \
    --output "$temporary_directory/config-overridden.csv"
[ "$(awk -F, 'NR == 2 { print $4, $7 }' "$temporary_directory/config-overridden.csv")" \
    = "fcfs 2" ] || fail "CLI nao prevaleceu sobre a configuracao"

(
    cd "$temporary_directory"
    "$simulator" --processes 1 >/dev/null
)
[ ! -e "$temporary_directory/load.csv" ] || fail "load.csv foi criado implicitamente"

"$simulator" \
    --processes 2 \
    --workload-input "$temporary_directory/workload.csv" \
    --workload-output "$temporary_directory/workload-roundtrip.csv" \
    >/dev/null
cmp "$temporary_directory/workload.csv" "$temporary_directory/workload-roundtrip.csv" \
    || fail "round-trip do workload nao foi identico"

"$simulator" \
    --processes 2 \
    --workload-input "$temporary_directory/workload.csv" \
    --run-id imported \
    --output "$temporary_directory/imported-result.csv"
original_hash=$(awk -F, 'NR == 2 { print $3 }' "$temporary_directory/result.csv")
imported_hash=$(awk -F, 'NR == 2 { print $3 }' "$temporary_directory/imported-result.csv")
[ "$original_hash" = "$imported_hash" ] || fail "hash mudou apos importacao"
[ "$(printf %s "$original_hash" | wc -c)" -eq 64 ] \
    || fail "SHA-256 nao possui 64 caracteres hexadecimais"

"$simulator" \
    --processes 2 \
    --run-id individual \
    --individual-output "$temporary_directory/individual.csv" \
    >/dev/null
[ "$(sed -n '1p' "$temporary_directory/individual.csv")" = "$individual_header" ] \
    || fail "schema individual incorreto"
[ "$(awk -F, 'NR == 2 { print NF }' "$temporary_directory/individual.csv")" = 11 ] \
    || fail "metrica individual nao possui 11 campos"
[ "$(wc -l < "$temporary_directory/individual.csv")" -eq 3 ] \
    || fail "quantidade de metricas individuais incorreta"

printf '%s\n' \
    'schema_version=1' \
    'scenario=equilibrado' \
    'seed=1' \
    'process_count=1' \
    'process_count=2' \
    'context_switch_cost=1' \
    'rr_quantum=4' \
    'arrival_min=0' \
    'arrival_max=3' > "$temporary_directory/duplicate.conf"
expect_failure "$simulator" --config "$temporary_directory/duplicate.conf" --self-test

printf '%s\n' \
    'schema_version=1' \
    'unknown_key=1' > "$temporary_directory/unknown.conf"
expect_failure "$simulator" --config "$temporary_directory/unknown.conf" --self-test

printf '%s\n' \
    'schema_version=1' \
    'process_count=0' > "$temporary_directory/invalid-value.conf"
expect_failure "$simulator" --config "$temporary_directory/invalid-value.conf" --self-test

printf '%s\n' \
    'schema_version=1' \
    'rr_quantum=0' > "$temporary_directory/invalid-quantum.conf"
expect_failure "$simulator" --config "$temporary_directory/invalid-quantum.conf" --self-test
expect_failure "$simulator" --algorithm rr --processes 1 --rr-quantum 0
expect_failure "$simulator" --algorithm rr --processes 1 --rr-quantum -1

printf '%s\n' \
    'pid,arrival,priority,burst_index,burst_type,duration' \
    '1,0,3,1,CPU,2' \
    '1,0,3,2,CPU,1' > "$temporary_directory/invalid-workload.csv"
expect_failure "$simulator" --processes 1 \
    --workload-input "$temporary_directory/invalid-workload.csv"
expect_failure "$simulator" --processes 1 --output "$temporary_directory/no-run-id.csv"
expect_failure "$simulator" --processes 3 \
    --workload-input "$temporary_directory/workload.csv"

expect_failure "$simulator" \
    --processes 1 \
    --run-id alias-collision \
    --output "$temporary_directory/alias-result.csv" \
    --individual-output "$temporary_directory/sub/../alias-result.csv"
[ ! -e "$temporary_directory/alias-result.csv" ] \
    || fail "colisao entre caminhos equivalentes publicou um resultado"

cp "$temporary_directory/valid.conf" "$temporary_directory/config-preserved.conf"
cp "$temporary_directory/config-preserved.conf" "$temporary_directory/config-preserved.expected"
expect_failure "$simulator" \
    --config "$temporary_directory/config-preserved.conf" \
    --workload-output "$temporary_directory/sub/../config-preserved.conf" \
    --self-test
cmp "$temporary_directory/config-preserved.expected" "$temporary_directory/config-preserved.conf" \
    || fail "arquivo de configuracao foi alterado por um caminho equivalente"

ln -s config-preserved.conf "$temporary_directory/config-link.conf"
expect_failure "$simulator" \
    --config "$temporary_directory/config-preserved.conf" \
    --run-id symlink-collision \
    --output "$temporary_directory/config-link.conf"
cmp "$temporary_directory/config-preserved.expected" "$temporary_directory/config-preserved.conf" \
    || fail "arquivo de configuracao foi alterado por um link simbolico"

expect_failure "$simulator" \
    --processes 1 \
    --run-id io-failure \
    --individual-output /dev/full \
    --output "$temporary_directory/must-not-exist.csv"
[ ! -e "$temporary_directory/must-not-exist.csv" ] \
    || fail "resultado agregado foi publicado apos falha da saida individual"

expect_failure "$simulator" \
    --processes 1 \
    --run-id transaction-failure \
    --workload-output "$temporary_directory/must-not-exist-workload.csv" \
    --individual-output "$temporary_directory/must-not-exist-individual.csv" \
    --output "/proc/simulador-issue30-result-$$.csv"
[ ! -e "$temporary_directory/must-not-exist-workload.csv" ] \
    || fail "workload foi publicado apos falha de outra saida"
[ ! -e "$temporary_directory/must-not-exist-individual.csv" ] \
    || fail "metricas individuais foram publicadas apos falha de outra saida"

printf '%s\n' 'conteudo-anterior' > "$temporary_directory/preserved-individual.csv"
mkdir "$temporary_directory/output-is-directory"
expect_failure "$simulator" \
    --processes 1 \
    --run-id rollback \
    --individual-output "$temporary_directory/preserved-individual.csv" \
    --output "$temporary_directory/output-is-directory"
[ "$(cat "$temporary_directory/preserved-individual.csv")" = 'conteudo-anterior' ] \
    || fail "saida anterior nao foi restaurada apos falha de publicacao"

if find "$temporary_directory" \( -name '*.tmp.*' -o -name '*.bak.*' \) -print | grep -q .; then
    fail "arquivo temporario ou backup transacional nao foi removido"
fi

echo "Testes de CLI, schemas, round-trip, hash, colisoes e E/S passaram."
