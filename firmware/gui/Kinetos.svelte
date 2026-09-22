<script>
	// Kinetos OpenEVSE V5: settings for the Kinetos board additions (§14a, LED ring, smart1,
	// Home Assistant, TeslaMate) plus live MID meter values and diagnostics.
	import { onMount } 			from "svelte";
	import { _ } 				from 'svelte-i18n'
	import { status_store } 	from "./../../../lib/stores/status.js";
	import { config_store } 	from "../../../lib/stores/config.js";
	import serialQueue 			from "../../../lib/queue.js";
	import Box 					from "../../ui/Box.svelte";
	import Borders 				from "./../../ui/Borders.svelte";
	import Switch 				from "../../ui/Switch.svelte";
	import InputForm 			from "../../ui/InputForm.svelte";
	import Select 				from "../../ui/Select.svelte";
	import Button 				from "../../ui/Button.svelte";

	let mounted = false
	let f = {}
	let inputs = {}
	let tmCar = 1
	let p14aTest = ""

	const keys = ["p14a_enabled", "p14a_pin", "p14a_active_high", "p14a_limit", "led_pv_enabled",
		"led_fx_charge", "led_brightness", "mqtt_grid_ie", "mqtt_solar", "smart1_enabled",
		"ha_discovery_enabled", "is_threephase"]

	const pins = [{name: "IO14", value: 14}, {name: "IO15", value: 15}, {name: $_("config.kinetos.p14a-pin-none"), value: 255}]
	const polarity = [{name: $_("config.kinetos.p14a-low"), value: false}, {name: $_("config.kinetos.p14a-high"), value: true}]
	const fx = [[0, "Static"], [2, "Breath"], [3, "Color wipe"], [5, "Color wipe reverse"], [11, "Rainbow"],
		[12, "Rainbow cycle"], [15, "Fade"], [16, "Theater chase"], [18, "Running lights"], [21, "Twinkle fade"],
		[31, "Chase color"], [40, "Running color"], [43, "Larson scanner"], [44, "Comet"], [45, "Fireworks"],
		[49, "Fire flicker"], [55, "Twinklefox"], [56, "Rain"]].map(([v, n]) => ({name: n + " (" + v + ")", value: v}))

	const load = () => { keys.forEach(k => f[k] = $config_store[k]) }

	const set = async (key) => {
		const input = inputs[key]
		input?.setStatus("loading")
		let val = f[key]
		if (typeof $config_store[key] == "number") val = Number(val)
		const ok = await serialQueue.add(() => config_store.upload({[key]: val}))
		input?.setStatus(ok ? "ok" : "error")
	}

	const teslamate = async () => {
		const b = "teslamate/cars/" + (tmCar || 1) + "/"
		await serialQueue.add(() => config_store.upload({
			vehicle_data_src: 2, mqtt_vehicle_soc: b + "battery_level",
			mqtt_vehicle_range: b + "est_battery_range_km", mqtt_vehicle_eta: b + "time_to_full_charge",
			mqtt_vehicle_range_miles: false}))
	}

	const p14a = async (on) => {
		const r = await fetch("/kinetos/p14a", {method: "POST", body: on ? "1" : "0"})
		p14aTest = r.ok ? "" : $_("config.kinetos.p14a-test-error")
	}

	const num = (v, d = 1) => (v === undefined || v === null || v === false) ? "–" : Number(v).toFixed(d)

	onMount(() => { load(); mounted = true })
</script>

{#if mounted}
<Box title={$_("config.titles.kinetos")} icon="mdi:ev-station" back={true}>
	<div class="columns is-centered">
		<div class="column is-three-quarters is-full-mobile">

			<!-- Live MID meter -->
			<Borders grow>
				<div class="has-text-weight-bold mb-2">{$_("config.kinetos.live")}</div>
				<div class="columns is-multiline is-mobile is-size-7">
					<div class="column is-half">{$_("config.kinetos.power")}: <b>{num($status_store.power, 0)} W</b> · {$_("config.kinetos.phases")}: <b>{$status_store.phases ?? "–"}</b></div>
					<div class="column is-half">{$_("config.kinetos.mid")}: <b>{num($status_store.mid_import_kwh, 2)} kWh</b></div>
					<div class="column is-half">{$_("config.kinetos.current")}: <b>{num($status_store.amp)} / {num($status_store.amp2)} / {num($status_store.amp3)} A</b></div>
					<div class="column is-half">{$_("config.kinetos.voltage")}: <b>{num($status_store.voltage, 0)} / {num($status_store.voltage2, 0)} / {num($status_store.voltage3, 0)} V</b></div>
					<div class="column is-half">§14a: <b class={$status_store.p14a_active ? "has-text-warning-dark" : ""}>{$status_store.p14a_active ? $_("active") + " (" + $status_store.p14a_limit_a + " A)" : $_("disabled")}</b></div>
					<div class="column is-half">{$_("config.kinetos.pvshare")}: <b>{$status_store.pv_share >= 0 ? Math.round($status_store.pv_share * 100) + " %" : "–"}</b></div>
				</div>
			</Borders>

			<!-- §14a -->
			<div class="has-text-weight-bold mt-4 mb-1">{$_("config.kinetos.p14a-title")}</div>
			<div class="is-size-7 mb-2">{$_("config.kinetos.p14a-desc")} <code>{$config_store.mqtt_topic}/p14a/set</code></div>
			<Borders grow>
				<div class="mb-2"><Switch name="p14a_enabled" label={$_("enable")} bind:this={inputs.p14a_enabled} bind:checked={f.p14a_enabled} onChange={() => set("p14a_enabled")} /></div>
				<div class="mb-2"><Select title={$_("config.kinetos.p14a-pin")} bind:this={inputs.p14a_pin} bind:value={f.p14a_pin} items={pins} onChange={() => set("p14a_pin")} /></div>
				<div class="mb-2"><Select title={$_("config.kinetos.p14a-polarity")} bind:this={inputs.p14a_active_high} bind:value={f.p14a_active_high} items={polarity} onChange={() => set("p14a_active_high")} /></div>
				<div class="mb-2"><InputForm title={$_("config.kinetos.p14a-limit")} type="number" min=1400 max=22000 step=100 bind:this={inputs.p14a_limit} bind:value={f.p14a_limit} onChange={() => set("p14a_limit")} /></div>
				<div class="mt-2">
					<Button name={$_("config.kinetos.p14a-test-on")} color="is-warning" butn_submit={() => p14a(true)} />
					<Button name={$_("config.kinetos.p14a-test-off")} butn_submit={() => p14a(false)} />
					{#if p14aTest}<div class="is-size-7 has-text-danger">{p14aTest}</div>{/if}
				</div>
			</Borders>

			<!-- LED ring -->
			<div class="has-text-weight-bold mt-4 mb-1">{$_("config.kinetos.led-title")}</div>
			<Borders grow>
				<div class="mb-2"><Switch name="led_pv_enabled" label={$_("config.kinetos.led-pv")} bind:this={inputs.led_pv_enabled} bind:checked={f.led_pv_enabled} onChange={() => set("led_pv_enabled")} /></div>
				<div class="mb-2"><Select title={$_("config.kinetos.led-fx")} bind:this={inputs.led_fx_charge} bind:value={f.led_fx_charge} items={fx} onChange={() => set("led_fx_charge")} /></div>
				<div class="mb-2"><InputForm title={$_("config.kinetos.led-brightness")} type="number" min=0 max=255 bind:this={inputs.led_brightness} bind:value={f.led_brightness} onChange={() => set("led_brightness")} /></div>
				<div class="mb-2"><InputForm title={$_("config.kinetos.grid-topic")} placeholder="home/grid/power" bind:this={inputs.mqtt_grid_ie} bind:value={f.mqtt_grid_ie} onChange={() => set("mqtt_grid_ie")} /></div>
				<div class="mb-2"><InputForm title={$_("config.kinetos.solar-topic")} bind:this={inputs.mqtt_solar} bind:value={f.mqtt_solar} onChange={() => set("mqtt_solar")} /></div>
			</Borders>

			<!-- Interfaces -->
			<div class="has-text-weight-bold mt-4 mb-1">{$_("config.kinetos.interfaces")}</div>
			<Borders grow>
				<div class="mb-2"><Switch name="smart1_enabled" label={$_("config.kinetos.smart1")} bind:this={inputs.smart1_enabled} bind:checked={f.smart1_enabled} onChange={() => set("smart1_enabled")} /></div>
				<div class="mb-2"><Switch name="ha_discovery_enabled" label={$_("config.kinetos.ha")} bind:this={inputs.ha_discovery_enabled} bind:checked={f.ha_discovery_enabled} onChange={() => set("ha_discovery_enabled")} /></div>
				<div class="mb-2"><Switch name="is_threephase" label={$_("config.kinetos.threephase")} bind:this={inputs.is_threephase} bind:checked={f.is_threephase} onChange={() => set("is_threephase")} /></div>
			</Borders>

			<!-- TeslaMate -->
			<div class="has-text-weight-bold mt-4 mb-1">TeslaMate</div>
			<div class="is-size-7 mb-2">{$_("config.kinetos.teslamate-desc")}</div>
			<Borders grow>
				<div class="mb-2"><InputForm title={$_("config.kinetos.teslamate-car")} type="number" min=1 max=20 bind:value={tmCar} /></div>
				<Button name={$_("config.kinetos.teslamate-apply")} butn_submit={teslamate} />
				{#if $config_store.vehicle_data_src == 2}<div class="is-size-7 mt-1">{$config_store.mqtt_vehicle_soc}</div>{/if}
			</Borders>

			<!-- Diagnostics -->
			<div class="has-text-weight-bold mt-4 mb-1">{$_("config.kinetos.diag")}</div>
			<Borders grow>
				<div class="is-size-7">
					{$_("config.kinetos.uptime")}: <b>{Math.round(($status_store.uptime_ms || 0) / 60000)} min</b> ·
					{$_("config.kinetos.reset")}: <b>{$status_store.reset_reason}</b> ·
					MID: <b>{$status_store.mid_responses}/{$status_store.mid_requests}</b> ({$status_store.mid_errors} {$_("error")}) ·
					IO14/IO15/IO39: <b>{$status_store.gpio14}/{$status_store.gpio15}/{$status_store.gpio39}</b>
					{#if $status_store.last_panic}<div class="has-text-danger">Panic: {$status_store.last_panic.reason} @ {$status_store.last_panic.pc}</div>{/if}
				</div>
			</Borders>
		</div>
	</div>
</Box>
{/if}
