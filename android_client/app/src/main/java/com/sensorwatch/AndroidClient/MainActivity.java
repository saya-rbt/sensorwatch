package com.sensorwatch.AndroidClient;

import android.os.Bundle;
import android.view.View;
import android.widget.ArrayAdapter;
import android.widget.Spinner;
import android.widget.TextView;

import androidx.appcompat.app.AppCompatActivity;

import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetAddress;
import java.net.SocketException;

public class MainActivity extends AppCompatActivity {

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        final Spinner spFirst = (Spinner)findViewById(R.id.spinFirst);
        final Spinner spSecond = (Spinner)findViewById(R.id.spinSecond);
        final Spinner spThird = (Spinner)findViewById(R.id.spinThird);
        // Create an ArrayAdapter using the string array and a default spinner layout
        ArrayAdapter<CharSequence> adapter = ArrayAdapter.createFromResource(this,
                R.array.planets_array, android.R.layout.simple_spinner_item);
        // Specify the layout to use when the list of choices appears
        adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        // Apply the adapter to the spinner
        spFirst.setAdapter(adapter);
        spSecond.setAdapter(adapter);
        spThird.setAdapter(adapter);
        spFirst.setSelection(0);
        spSecond.setSelection(1);
        spThird.setSelection(2);

        findViewById(R.id.btSendMessage).setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                int nbrepet = 1;
                String data = spFirst.getSelectedItem().toString().substring(0,1) +  spSecond.getSelectedItem().toString().substring(0,1) + spThird.getSelectedItem().toString().substring(0,1)  ;
                int port = Integer.valueOf(((TextView) findViewById(R.id.EdPort)).getText().toString());
                String address = ((TextView) findViewById(R.id.EdIpServeur)).getText().toString();
                SendData(nbrepet,data,port,address);
            }
        });
    }



    private DatagramSocket UDPSocket;
    private InetAddress address;
    private int port;

    /// Initialise une socket avec les parametres recupere dans l'interface graphique pour l'envoi des données
    public void Initreseau(InetAddress address) {
        try {
            this.UDPSocket = new DatagramSocket();
            this.address = address;
        } catch (SocketException e) {
            e.printStackTrace();
        }
    }

    /// Envoi les données dans la socket defini par la methode InitReseau
    public void SendInstruction(final byte[] data, final int port) {
        new Thread() {
            @Override
            public void run() {
                try {
                    DatagramPacket packet = new DatagramPacket(data, data.length, address, port);
                    UDPSocket.send(packet);
                    DatagramPacket packetreponse = null;
                    UDPSocket.receive(packetreponse);
                    DisplayData(packetreponse);
                } catch (Exception e) {
                    e.printStackTrace();
                }

            }
        }.start();
    }

    /// Envoi X fois la data
    public void SendData(final int nbRepet, final String Sdata , final int port, final String address) {
        new Thread() {
            @Override
            public void run() {
                try {
                    Initreseau(InetAddress.getByName(address));
                    for (int i = 0; i < nbRepet; i++) {
                        byte[] data = Sdata.getBytes();
                        SendInstruction(data,port);
                    }
                } catch (Exception e) {
                    e.printStackTrace();
                }
            }
        }.start();

    }

    // Modifie l affichage en fonction de la tram recu
    public void DisplayData(DatagramPacket data) {
        System.out.println(data);
    }

}
