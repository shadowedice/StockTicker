# StockTicker
### Updated: The API used to queury the stock information is now unavailable. Modifications to the source will be needed in order to pull stock information

This project uses an ESP8266 and two 8x32 LED matrix panels (WS2812B LEDs) to show current stock information. The user is able to update the Wifi information through the included file or via the serial port. The user can also add or remove different stock symbols by going to the IP address that is shown when the ESP8266 connects. Each stock symbol is queried before it is displayed to show the most relavant information and the stock API is being provided by iextrading.
