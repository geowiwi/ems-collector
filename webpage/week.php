<html>

<head>
  <meta http-equiv="Content-Type" content="text/html; charset=UTF-8">
  <title>Heizung</title>

  <link href="style.css" type="text/css" rel="stylesheet"/>
</head>

<?php
include 'sensor_utils.php.inc';
include 'utils.php.inc';

set_loc_settings();

$aussentemp = get_min_max(SensorAussenTemp, "week");
$raumtemp = get_min_max(SensorRaumIstTemp, "week");
?>

<body topmargin=0 leftmargin=0 marginwidth=0 marginheight=0>
  <h2>Letzte Woche</h2>
  <table border=0 cellspacing=0 cellpadding=0>
    <tr><td>
      <?php print_min_max_table("Außentemperatur", $aussentemp); ?>
    </td></tr>
    <tr height=6></tr>
    <tr><td>
      <?php print_min_max_table("Raumtemperatur", $raumtemp); ?>
    </td></tr>
  </table>
  <h3>Graphen</h3>
  <p>
    <img src="graphs/aussentemp-week.png" alt="Außentemperaturentwicklung">
  </p>
  <p>
    <img src="graphs/raumtemp-week.png" alt="Raumtemperaturentwicklung">
  </p>
  <p>
    <img src="graphs/kessel-week.png" alt="Kesseltemperaturentwicklung">
  </p>
  <p>
    <img src="graphs/ww-week.png" alt="Warmwassertemperaturentwicklung">
  </p>
</body>

</html>

