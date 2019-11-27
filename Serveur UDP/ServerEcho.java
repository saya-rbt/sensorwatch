import java.net.*; 
import java.io.*;

/** <p>Exemple de serveur Java.</p>
 *  <p>Il écoute sur un numéro de port pris en paramètre de ligne de commande. 
 *  Tout message reçu est renvoyé inchangé à l'expéditeur.</p>
 *  <p>Exemple d'usage : java Server 5000</p>
 **/

public class ServerEcho {

    /** Prend en paramètre le numéro de port sur lequel écouter. */
    public static void main(String[] args) {
        String param ="8081";
        if(args.length!=0) param=args[0];
        else System.out.println("Pas de paramètre fourni. Utilisation du port "+param+" par défaut.");
        
        try{
            // Le numéro de port lu en ligne de commande est utilisé pour activer le socket 
            doService(Integer.parseInt(param));
    
        }catch (Exception e){
        // Traitement bien moche des différents cas d'erreur.
          System.out.println("Erreur.");
          e.printStackTrace();
        }
    }


    static void doService(int port) throws java.io.IOException{
        // Mise en place du socket d'accès réseau
        DatagramSocket socket = new DatagramSocket(port);
        
        // Structure de donnée de réception et émission 
        final int MTU = 1500; // MTU = Maximum Transmission Unit : taille max prévue par les protocoles Wifi et Ethernet
        byte[] data;
        
        System.out.println("Waiting on port "+socket.getLocalPort()+"...");
        while(true){
            data = new byte[MTU];
            DatagramPacket packet= new DatagramPacket(data, MTU);
            // Lecture bloquante d'1 byte à la fois et réémission du byte reçu.
            socket.receive(packet);
            System.out.println("Received "+packet.getLength()+" bytes from "+packet.getAddress()+":"+packet.getPort());
            afficheData(data,packet.getLength());
          
            String answer = new String(packet.getData());
           // Renvoie à la source du même message
            if (answer.contains("getValues()"))
            {
                System.out.println(answer);
                byte[] dataGet = "20.6;3.0;58.2;".getBytes();
                packet.setData(dataGet);               
                socket.send(packet);
            }
            else
            {
                socket.send(packet);
            }
            
        }
    }
    
    static void afficheData(byte[] data, int length){
        if(length==0) {
            System.out.println("   {}");
        } else {
            System.out.print("   {");
            for(int i=0 ; i<length-1 ; i++){
                System.out.print(data[i]+", ");
            }
            System.out.print(data[length-1]+"}");
            System.out.println("("+(new String(data))+")");
        }
    }
}
