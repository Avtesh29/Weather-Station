from http.server import BaseHTTPRequestHandler, HTTPServer
import json
import subprocess # For running curl

# --- Configuration ---
HOST_NAME = '0.0.0.0'  # Listen on all available interfaces
PORT_NUMBER = 1234
# LOCATION_TO_SERVE is no longer needed here as it will be fetched dynamically

class MyServer(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == '/location':
            try:
                # Execute curl command to get location info from ipinfo.io
                process = subprocess.run(['curl', '-s', 'https://ipinfo.io/json'], capture_output=True, text=True, check=True)
                ip_info = json.loads(process.stdout)
                city = ip_info.get('city', 'UnknownLocation') # Get city, or a default if not found

                # Replace spaces with '+'
                city_formatted = city.replace(' ', '+')

                self.send_response(200)
                self.send_header('Content-type', 'text/plain')
                self.end_headers()
                self.wfile.write(city_formatted.encode('utf-8'))
                # print(f"Served dynamic location: {city}")

            except subprocess.CalledProcessError as e:
                print(f"Error calling curl: {e}")
                self.send_response(500) # Internal Server Error
                self.send_header('Content-type', 'text/plain')
                self.end_headers()
                self.wfile.write("Error fetching location (curl failed)".encode('utf-8'))
            except json.JSONDecodeError as e:
                print(f"Error decoding JSON from ipinfo.io: {e}")
                self.send_response(500)
                self.send_header('Content-type', 'text/plain')
                self.end_headers()
                self.wfile.write("Error fetching location (JSON decode error)".encode('utf-8'))
            except Exception as e:
                print(f"An unexpected error occurred while fetching location: {e}")
                self.send_response(500)
                self.send_header('Content-type', 'text/plain')
                self.end_headers()
                self.wfile.write("Error fetching location (Unexpected error)".encode('utf-8'))
        else:
            self.send_response(404)
            self.send_header('Content-type', 'text/plain')
            self.end_headers()
            self.wfile.write("Not Found".encode('utf-8'))

    def do_POST(self):
        content_length = int(self.headers['Content-Length'])
        post_data = self.rfile.read(content_length)
        
        data_str = post_data.decode('utf-8')
        
        print(f"Received POST data:\n{data_str}")

        # For Lab 7.2 and 7.3, this is where you'd display/process
        # the temperature and humidity from the ESP32
        
        # Example parsing (uncomment and adapt if needed):
        # from urllib.parse import parse_qs
        # parsed_data = parse_qs(data_str)
        # temperature = parsed_data.get('temperature', [None])[0]
        # humidity = parsed_data.get('humidity', [None])[0]
        # print(f"Parsed Temperature: {temperature}, Humidity: {humidity}")

        self.send_response(200)
        self.send_header('Content-type', 'text/plain')
        self.end_headers()
        self.wfile.write("POST request received".encode('utf-8'))

if __name__ == '__main__':
    httpd = HTTPServer((HOST_NAME, PORT_NUMBER), MyServer)
    print(f"Server starting on http://{HOST_NAME}:{PORT_NUMBER}")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass
    httpd.server_close()
    print("\nServer stopped.")