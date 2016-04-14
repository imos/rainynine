<?
error_reporting(E_ALL ^ E_NOTICE);

if ($argv[1] == 'DEBUG') define('DEBUG', true); else define('DEBUG', false);
require_once('lib/oauth.php');
require_once('lib/location.php');

define('OAUTH_CONSUMER_KEY', '********');
define('OAUTH_CONSUMER_SECRET',
    '********');
date_default_timezone_set('Asia/Tokyo');
ini_set('user_agent',
    'User-Agent: Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1)');

function twitter_update($status, $token, $secret) {
	return json_decode(oauth_request(
	    'https://api.twitter.com/1.1/statuses/update.json',
	    array('oauth_token' => $token, 'oauth_token_secret' => $secret) +
	    array('status' => $status, 'in_reply_to_status_id' => $in_reply_to),
	    'POST'));
}

function rainynine_load($location, $position, $time) {
	$target_code = date('Hi', $time);
	$data_file = "/tmp/forecast/$location-$target_code-$position.txt";
	if (@filemtime($data_file) < $time) return false;
	$prob = array();
	foreach (explode("\n", file_get_contents($data_file)) as $line) {
		$line = preg_split('%\s+%', $line);
		if (count($line) < 6) continue;
		$real_time = $time + intval($line[0] / 2) * 60;
		if ($line[0]) {
			$prob[$real_time]['x'] = (float)$line[1] / intval($line[0]);
			$prob[$real_time]['y'] = (float)$line[2] / intval($line[0]);
		}
		for ($j = 4; $j < count($line); $j += 2) {
			$color = (intval($line[$j]) & 0xf0) >> 4;
			// if ($color == 15) continue;
			$prob[$real_time][$color] += intval($line[$j + 1]);
			$prob[$real_time]['sum'] += intval($line[$j + 1]);
		}
	}
	ksort($prob);
	foreach ($prob as $key => $val) {
		if (!$val['sum']) continue;
		for ($i = 0; $i < 16; $i++) {
			$prob[$key][$i] = (float)$val[$i] / $val['sum'];
		}
	}
	return $prob;
}

function rainynine_generate($location, $position, $info, $time = false) {
	global $tweets;
	$twitter = $GLOBALS['locations'][$location][$position];
	if (!$time) $time = time();
	echo "Generating $location-$position...\n";
	$time = intval($time / 60) * 60;
	$stat = array();
	for ($i = 0; $i <= 25; $i++) {
		$data = rainynine_load($location, $position, $time - $i * 60);
		if (!$data) continue;
		foreach ($data as $key => $val) {
			$stat[$key][$i] = $val;
		}
	}
	$results = array();
	foreach ($stat as $target_time => $target_data) {
		$result = array(); $sum = 0;
		if (count($target_data) < 3) continue;
		foreach ($target_data as $diff_time => $data) {
			$forecast_time = $time - $diff_time * 60;
			$certainty = 1.0 / (120 + abs($forecast_time - $target_time));
			$sum += $certainty;
			foreach ($data as $key => $val) {
				$result[$key] += $val * $certainty;
			}
		}
		foreach ($result as $key => $val) {
			$result[$key] /= $sum;
		}
		$results[$target_time] = $result;
	}
	$position2 = str_replace('-', '_', $position);
	$out = "function update_$position2(data) {\n";
	$rows = 151;
	$out .= "\tdata.addRows(<<<ROWS>>>);\n";
	$out_count = 0;
	$pbout = "location <\n";
	$pbout .= "  name: \"{$twitter['name']}\"\n";
	$pbout .= "  latitude: {$twitter['coordinate'][0]}\n";
	$pbout .= "  longtitude: {$twitter['coordinate'][1]}\n";
	$pbout .= ">\n";
	$lower_bounds = array(
	    1 => 0.1, 2 => 1, 3 => 5, 4 => 10, 5 => 20, 6 => 50, 7 => 100);
	for ($p = 0; $p < $rows; $p++) {
		$target_time = $time + $p * 60;
		$date = date('m月d日 H時i分', $target_time);
		$info = $results[$target_time];
		for ($i = 14; 0 <= $i; $i--) $info[$i] += $info[$i + 1];
		if (0.5 < $info[8]) break;
		for ($i = 1; $i <= 7; $i++) {
		  $info[$i] = ($info[$i] - $info[8]) / (1 - $info[8]);
		}
		$out .= "\tdata.setValue($p, 0, '$date');\n";
		$pbout .= "data <\n";
		$pbout .= "  time: $target_time\n";
		for ($i = 1; $i <= 7; $i++) {
			$value = round($info[$i] * 100);
			$out .= "\tdata.setValue($p, $i, $value);\n";
			$pbout .= "  percentages <\n";
			$pbout .= "    lower_bound: " . $lower_bounds[$i] . "\n";
			$pbout .= "    percentage: $value\n";
			$pbout .= "  >\n";
		}
		$pbout .= ">\n";
		$out_count++;
	}
	$out .= "}\n";
	$out = str_replace('<<<ROWS>>>', $out_count, $out);
	file_put_contents("/var/www/sx9.jp/weather/$position.js", $out);
	file_put_contents("/var/www/sx9.jp/weather/$position.pb", $pbout);
	$start_hour = strtotime(date('Y/m/d H:00:00', $time));
	$per_max = array(
	    (int)date('H', $start_hour) => 0.0,
	    (int)date('H', $start_hour + 3600) => 0.0);
	for ($p = $start_hour; $p < $start_hour + 10800; $p += 60) {
		$info = $results[$p];
		$hour = (int)date('H', $p);
		if (!$info['sum'] || 0.6 < $info[15]) continue;
		$per = (1 - $info[0] - $info[15]) / (1 - $info[15]);
		$per_max[$hour] = max($per_max[$hour], $per);
	}
	$out = $twitter['short'];
	$tmp = array();
	$value = 0;
	foreach ($per_max as $key => $val) {
		$tmp[] = $key . '時台' . round($val * 100) . '％';
		$value = max($value, round($val * 100));
	}
	$out .= '（' . implode('，', $tmp) . '）';
	$tweets[$twitter['token']]['per']['id'] = intval($time / 1800);
	$tweets[$twitter['token']]['per']['value'] =
	 max($tweets[$twitter['token']]['per']['value'], $value);
	$tweets[$twitter['token']]['per']['msg'][
	    sprintf("%03d", 100 - $value) . $position] = $out;
}

if (DEBUG) echo "DEBUG mode!\n";

$tweets = array();
foreach ($locations as $location => $positions) {
	if (!is_array($positions)) continue;
	foreach ($positions as $position => $info) {
		if (!$info) continue;
		$tweets[$locations[$location][$position]['token']]['secret']
		 = $locations[$location][$position]['secret'];
		$tweets[$locations[$location][$position]['token']]['footer']
		 = $locations[$location][$position]['footer'];
		if (time() < strtotime('2011/05/31 06:00')) {
			$time = strtotime('2011/05/30 06:00');
			$time += intval(date('i')) * 60;
		} else $time = time();
		rainynine_generate($location, $position, $info, $time);
	}
}

foreach ($tweets as $token => $info) {
	if (!$token) continue;
	$twitter_id = array_shift(explode('-', $token));
	$lock_file = "/var/www/sx9.jp/weather/$twitter_id.lock";
	$data_file = "/var/www/sx9.jp/weather/$twitter_id-memory.js";
	$lp = fopen($lock_file, 'a+');
	flock($lp, LOCK_EX);
	$record = @json_decode(file_get_contents($data_file), true);
	if (!$record) $record = array();
	if ($info['footer']) $record['footer'] = trim($info['footer']);
	echo "mode(per): ";
	if ($record['per'] != $info['per']['id']) {
		$msg = "0.1㍉以上の降水確率は，\n";
		ksort($info['per']['msg']);
		$msg .= implode("，\n", $info['per']['msg']);
		$msg .= "\nなのっ";
		$msg .= $info['footer'];
		$msg .= ' #rainy_nine';
		echo $msg . "\n";
		if (30 < $info['per']['value']) {
			twitter_update($msg, $token, $info['secret']);
			echo "POSTED\n";
		} else echo "IGNORED\n";
		$record['per'] = $info['per']['id'];
	} else echo "DONE\n";
	file_put_contents($data_file, json_encode($record));
	fclose($lp);
}

print_r($tweets);
