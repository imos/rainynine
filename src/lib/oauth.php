<?php
/*
Imo OAuth Functions
Copyright(C) 2010/04/05-2010/04/05 Imajo Kentaro (imos at imoz.jp).

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/*******************************************************************************
OAuth 関数 (OAuth Functions)

定義の必要な定数 (Constants Required):
	OAUTH_CONSUMER_KEY, OAUTH_CONSUMER_SECRET.
*******************************************************************************/

/*******************************************************************************
oauth_request 関数 (oauth_request function)

導入 (Introduction):
	OAuthで接続し内容を取得する関数． (A function to get contents via OAuth.)

説明 (Description):
	string oauth_request($url [, $param = array() [, $method = 'GET']])
	
	OAuthで接続可能な $url に対してパラメータ $param を用いて
	HTTPメソッド $method で接続し，得られた内容を返します．
	
	Return the content from the $url by the HTTP method $method with $param.

制限事項 (Restriction):
	$url にはチルダまたは空白文字列を含むことはできません．
	
	$url cannot contain any tilde or space.
*******************************************************************************/

function oauth_request($url, $param = array(), $method = 'GET') {
	// Add necessary parameters
	$param += array(
	 'oauth_consumer_key' => OAUTH_CONSUMER_KEY,
	 'oauth_signature_method' => 'HMAC-SHA1',
	 'oauth_timestamp' => time(),
	 'oauth_nonce' => md5(microtime() . $_SERVER['REMOTE_ADDR']),
	 'oauth_version' => '1.0');
	
	// Sort parameters by key
	uksort($param, 'strnatcmp');
	
	// Generate seeds for HMAC Hash
	$key = OAUTH_CONSUMER_SECRET . "&$param[oauth_token_secret]";
	unset($param['oauth_token_secret']);
	$data = "$method&" . urlencode($url) . "&" . 
	 urlencode(strtr(http_build_query($param),
	 array('%7E' => '~', '+' => '%20')));
	
	// Replace signature into HMAC Hash
	$hash = base64_encode(hash_hmac('sha1', $data, $key, true));
	$param['oauth_signature'] = $hash;
	
	// Get the content
	$param = http_build_query($param);
	if ($method == 'GET') return file_get_contents("$url?$param");
	$header = array("Content-Type: application/x-www-form-urlencoded",
	 "Content-Length: " . strlen($param));
	$context = array("http" => array("method"  => "POST",
	 "header" => implode("\r\n", $header), "content" => $param));
	return file_get_contents($url, false, stream_context_create($context));
}
