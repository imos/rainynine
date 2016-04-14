<?
require_once('lib/location.php');

function xband_time($location) {
	$url = "http://www.river.go.jp/xbandradar/rdimg/$location/nowcast/$location-timedata01.html";
	if (!preg_match('%(\d{4}/\d\d/\d\d)\s+(\d\d:\d\d)%',
	    file_get_contents($url), $match)) return 'HOST_IS_DOWN';
	return strtotime($match[1] . ' ' . $match[2]);
}

function xband_url($time, $location) {
	$time = intval($time / 60) * 60;
	$sixhour = sprintf("%02d00", intval(date('H', $time) / 6) * 6);
	return "http://www.river.go.jp/xbandradar/rdimg/$location/" .
	 date('Ymd', $time) . '/' . $sixhour . '/' . $location .
	 date('Ymd', $time) . 'T' . date('Hi', $time) . '_detail.png';
}

function xband_flow($time, $location) {
	if ($time < strtotime('2011/01/01'))
	    die(date('Y/m/d H:i:s', $time) . " is too old.\n");
	$target_code = date('Hi', $time);
	if ($time < @filemtime(
	    "/tmp/forecast/$location-$target_code-flow.png")) return true;
	if (!is_dir('/tmp/forecast')) mkdir('/tmp/forecast');
	for ($shift = -10; $shift <= 0; $shift += 2) {
		$target_time = $time + $shift * 60;
		$target_code = date('Hi', $target_time);
		$files[] = "/tmp/forecast/$location-$target_code.gif";
		if ($target_time < @filemtime(
		    "/tmp/forecast/$location-$target_code.gif")) continue;
		echo "Loading $target_code...\n";
		if ($location == 'kanto11') {
			file_put_contents("/tmp/forecast/$location-$target_code-base.png",
			    file_get_contents(xband_url($target_time, $location)));
			exec("convert -crop '100%x100%+0+2730' " .
			     "/tmp/forecast/$location-$target_code-base.png " .
			     "/tmp/forecast/$location-$target_code.png ");
		} else {
			file_put_contents("/tmp/forecast/$location-$target_code.png",
			    file_get_contents(xband_url($target_time, $location)));
		}
		exec("~/bin/radar-convert -s 0.20254 -l xband " .
		    "-o /tmp/forecast/$location-$target_code.gif " .
		    "/tmp/forecast/$location-$target_code.png");
		if (!is_readable(
		    "/tmp/forecast/$location-$target_code.gif")) {
			die("$location-$target_code.gif is not found.\n");
		}
	}
	$files = array_reverse($files);
	echo "Calculating flow...\n";
	$target_code = date('Hi', $time);
	echo "~/bin/opticalflow -b 5 -s 2.5 -i white -a 0.3 " .
	 "-o /tmp/forecast/$location-$target_code-flow.png " .
	 implode(' ', $files);
	exec("~/bin/opticalflow -b 5 -s 2.5 -i white -a 0.3 " .
	 "-o /tmp/forecast/$location-$target_code-flow.png " .
	 implode(' ', $files));
	return is_readable("/tmp/forecast/$location-$target_code-flow.png");
}

function ninetan_forecast($time, $location, $position, $data) {
	$prob = array();
	foreach (explode("\n", $data) as $line) {
		$line = preg_split('%\s+%', $line);
		if (count($line) < 6) continue;
		if ($line[0]) {
			$prob[intval($line[0] / 2)]['x'] = (float)$line[1];
			$prob[intval($line[0] / 2)]['y'] = (float)$line[2];
		}
		for ($j = 4; $j < count($line); $j += 2) {
			$color = (intval($line[$j]) & 0xf0) >> 8;
			if ($color == 15) continue;
			$prob[intval($line[0] / 2)][$color] += intval($line[$j + 1]);
			$prob[intval($line[0] / 2)]['sum'] += intval($line[$j + 1]);
		}
	}
	foreach ($prob as $key => $val) {
		for ($i = 0; $i < 8; $i++) {
			$prob[$key][$i] = $prob[$key][$i] / $prob[$key]['sum'];
		}
	}
	ksort($prob);
	foreach ($prob as $key => $val) {
	
	}
}

function xband_forecast($time, $location) {
	global $locations;
	xband_flow($time, $location);
	$target_code = date('Hi', $time);
	foreach ($locations[$location] as $position => $value) {
		if (!$value) continue;
		echo "Calculating $position...\n";
		exec("~/bin/opticalflow -b 5 -s 2.5 -z 0.08 -d 0.25 " .
		 "-f /tmp/forecast/$location-$target_code-flow.png " .
		 "/tmp/forecast/$location-$target_code.gif " .
		 "-x {$value['location']['x']} -y {$value['location']['y']} " .
		 "> /tmp/forecast/$location-$target_code-$position.txt");
	}
}

function xband($location = 'kinki01') {
	$time = xband_time($location);
	xband_forecast($time, $location);
}

xband('kinki01');
xband('kanto11');
